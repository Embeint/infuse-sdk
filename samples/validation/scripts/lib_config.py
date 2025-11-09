#!/usr/bin/env python3

import argparse
import copy
import pathlib
import re
from http import HTTPStatus
from uuid import UUID

import pykwalify.core
import yaml
from infuse_iot import serial_comms
from infuse_iot.api_client import Client
from infuse_iot.api_client.api.board import get_board_by_id
from infuse_iot.api_client.api.device import (
    create_device,
    get_device_by_soc_and_mcu_id,
)
from infuse_iot.api_client.api.organisation import get_organisation_by_id
from infuse_iot.api_client.models import Device as InfuseIoTDevice
from infuse_iot.api_client.models import NewDevice
from infuse_iot.api_client.types import Response
from infuse_iot.util.soc import nrf
from intelhex import IntelHex
from lib_exceptions import ConfigurationError, ProvisioningError
from openhtf.util import configuration
from openhtf.util.units import Unit, UnitDescriptor
from pykwalify.errors import SchemaError


def abs_path_expand(path: str | None) -> str | None:
    "Convert path to absolute"

    return str(pathlib.Path(path).expanduser().resolve()) if path else None


class BinaryInfo:
    def __init__(self, info: dict):
        self.file: str = abs_path_expand(info["file"])
        self.erase: str | None = info.get("erase")
        self.verify: str | None = info.get("verify")
        self.reset: str | None = info.get("reset")
        self.provision: bool = info.get("provision", False)
        self.hex: IntelHex | None = IntelHex(self.file) if self.provision else None

    def hex_append(self, address: int, value: bytes, file: str) -> None:
        """Write the original file with extra data to file"""
        assert self.hex is not None
        # Copy the original file
        temp = copy.deepcopy(self.hex)
        # Insert in the new bytes
        temp[address : address + len(value)] = list(value)
        # Write the contents to file
        temp.write_hex_file(file)

    def nrfutil_option_args(self) -> list[str]:
        options = []
        if opt := self.reset:
            options += [f"reset={opt}"]
        if opt := self.erase:
            options += [f"chip_erase_mode={opt}"]
        if opt := self.verify:
            options += [f"verify={opt}"]

        if len(options) > 0:
            return ["--options", ",".join(options)]
        return []


class SocConfig:
    def __init__(self, config: dict):
        self.device_id: str = config["device-id"]
        self.jlink_snr: int = config["jlink-snr"]
        self.primary: bool = config.get("primary", False)
        self.recover: bool = config.get("recover", False)
        self.nrf91_modem_firmware: str | None = abs_path_expand(config.get("nrf91-modem-firmware"))
        self.binaries: dict[str, BinaryInfo] = {k: BinaryInfo(v) for k, v in config["binaries"].items()}
        self.serial_port: serial_comms.SerialLike
        if port := config.get("serial-port"):
            if "serial-baudrate" not in config:
                raise ConfigurationError("'serial-port' config requires 'serial-baudrate'")
            self.serial_port = serial_comms.SerialPort(port, config["serial-baudrate"])
        else:
            self.serial_port = serial_comms.RttPort(self.device_id, self.jlink_snr)


class InfuseConfig:
    def __init__(self, config: dict, api_key: str):
        self.organisation = config["organisation"]
        self.board = config["board"]
        self.api_key = api_key
        self._validate()

    @staticmethod
    def _http_err_msg(base: str, response: Response) -> str:
        return f"{base}:\n\t<{response.status_code}> {response.content.decode('utf-8')}"

    def _validate(self):
        if self.api_key is None:
            err = "Infuse-IoT provisioning requires '--infuse-api-key'"
            raise ConfigurationError(err)

        # Check Board and Organisation IDs
        with Client(base_url="https://api.infuse-iot.com").with_headers(
            {"x-api-key": f"Bearer {self.api_key}"}
        ) as client:
            response = get_board_by_id.sync_detailed(client=client, id=UUID(self.board))
            if response.status_code != HTTPStatus.OK:
                err = f"Failed to query board info:\n\t<{response.status_code}> {response.content.decode('utf-8')}"
                raise ConfigurationError(err)
            response = get_organisation_by_id.sync_detailed(client=client, id=UUID(self.organisation))
            if response.status_code != HTTPStatus.OK:
                err = (
                    f"Failed to query organisation info:\n\t<{response.status_code}> {response.content.decode('utf-8')}"
                )
                raise ConfigurationError(err)

    def _create_device(self, client, hardware_id: str, infuse_id: int | None) -> InfuseIoTDevice:
        new_device = NewDevice(
            mcu_id=hardware_id,
            organisation_id=self.organisation,
            board_id=self.board,
        )
        if infuse_id:
            new_device.device_id = f"{self._id:016x}"

        response = create_device.sync_detailed(client=client, body=new_device)
        if response.status_code != HTTPStatus.CREATED:
            raise ProvisioningError(self._http_err_msg("Failed to create device", response))
        return response.parsed

    def provision(self, interface: nrf.Interface, hardware_id: str, infuse_id: int | None = None) -> InfuseIoTDevice:
        client = Client(base_url="https://api.infuse-iot.com").with_headers({"x-api-key": f"Bearer {self.api_key}"})
        with client as client:
            response = get_device_by_soc_and_mcu_id.sync_detailed(
                client=client, soc=interface.soc_name, mcu_id=hardware_id
            )
            if response.status_code == HTTPStatus.OK:
                return response.parsed
            if response.status_code == HTTPStatus.NOT_FOUND:
                return self._create_device(client, hardware_id, infuse_id)
            else:
                raise ProvisioningError(self._http_err_msg("Failed to query device info", response))


class SubtestConfig:
    def __init__(self, config: dict):
        self.type = config["type"]
        self.float_equals: float | None = config.get("float-equals")
        self.int_equals: int | None = config.get("int-equals")
        self.str_equals: str | None = config.get("str-equals")
        self.float_in_range: list[float] | None = config.get("float-in-range")
        self.int_in_range: list[int] | None = config.get("int-in-range")
        try:
            self.unit: UnitDescriptor | None = Unit(config["unit"]) if "unit" in config else None
        except KeyError as e:
            err = f"Unit '{config['unit']}' does not exist"
            raise ConfigurationError(err) from e
        if self.type not in ["float", "int", "str"]:
            err = f"Unknown value type '{self.type}'"
            raise ConfigurationError(err)

    def convert(self, value: str):
        if self.type == "int":
            return int(value, 0)
        elif self.type == "float":
            return float(value)
        elif self.type == "str":
            return str(value)
        raise ConfigurationError(f"Unknown conversion type {self.type}")

    def equals(self) -> float | int | str | None:
        if self.float_equals:
            return self.float_equals
        if self.int_equals:
            return self.int_equals
        if self.str_equals:
            return self.str_equals
        return None

    def in_range(self) -> list[float] | list[int] | None:
        if self.float_in_range:
            return self.float_in_range
        if self.int_in_range:
            return self.int_in_range
        return None


class TestConfig:
    def __init__(self, tests: dict):
        self.subtests: dict[str, SubtestConfig] = (
            {k: SubtestConfig(v) for k, v in tests["subtests"].items()} if "subtests" in tests else {}
        )


class ValidationConfig:
    def __init__(
        self,
        args: argparse.Namespace,
        config: configuration._Configuration,
    ):
        self._config = config
        self.operator: str | None = None
        self._core_validate()
        self.tests: dict[str, TestConfig] = {k: TestConfig(v) for k, v in config.tests.items()}
        self.socs: dict[str, SocConfig] = {k: SocConfig(v) for k, v in config.socs.items()}
        self.primary_soc: SocConfig = self._primary_soc()
        self.infuse: InfuseConfig | None = None
        self.infuse_provisions = any(phase.provision for soc in self.socs.values() for phase in soc.binaries.values())
        if self.infuse_provisions:
            if "infuse" not in config:
                err = "Infuse-IoT provisioning required the 'infuse' configuration section"
                raise ConfigurationError(err)
            self.infuse = InfuseConfig(config["infuse"], args.infuse_api_key)
        self.test_timeout = config.general["test-timeout"]
        self.result_pattern: re.Pattern = re.compile(r"^(\d){6}:([^:]*):([^:]*):(.*)")
        self.val_pattern: re.Pattern = re.compile(r"^([^:]*):(.*)")

    def _core_validate(self) -> None:
        # Validate the provided configuration file
        CONFIG_SCHEMA_PATH = str(pathlib.Path(__file__).parent / "config-schema.yaml")
        with open(CONFIG_SCHEMA_PATH, encoding="utf-8") as f:
            config_schema = yaml.safe_load(f)

        try:
            pykwalify.core.Core(source_data=self._config._asdict(), schema_data=config_schema).validate()
        except SchemaError as e:
            raise ConfigurationError(str(e)) from e

        # Verify all firmware files exist
        for soc_name, soc_config in self._config.socs.items():
            for phase, flash_config in soc_config["binaries"].items():
                f = pathlib.Path(flash_config["file"])
                if not f.exists():
                    err = f"File '{str(f)}' does not exist for SoC '{soc_name}', Phase '{phase}'"
                    raise ConfigurationError(err)

    def _primary_soc(self) -> SocConfig:
        # Verify a single primary application exists
        primaries = [v for k, v in self.socs.items() if v.primary]
        if len(primaries) == 0:
            err = "Expected a single SoC with the 'primary' key"
            raise ConfigurationError(err)
        if len(primaries) != 1:
            err = "Multiple SoCs listed as 'primary'"
            raise ConfigurationError(err)

        return primaries[0]
