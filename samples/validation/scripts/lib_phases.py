#!/usr/bin/env python3

import os
import re
import subprocess
import tempfile
import time
from collections.abc import Generator

import openhtf
from infuse_iot.serial_comms import SerialLike
from infuse_iot.util.soc import nrf
from lib_config import ValidationConfig
from openhtf import PhaseResult, plugs
from openhtf.core import test_record
from openhtf.core.measurements import NotAMeasurementError, Outcome
from openhtf.plugs.user_input import UserInput
from openhtf.util import configuration

ONE_DAY = 60 * 60 * 24
CONF = configuration.CONF

operator_id: str | None = None


def retrieve_operator_id() -> str | None:
    return operator_id


@openhtf.PhaseOptions(name="Get Device Parameters")
@openhtf.measures(openhtf.Measurement("hardware_id"))
def get_device_id(test: openhtf.TestApi, config: ValidationConfig):
    """Get device information from SWD"""

    try:
        interface = nrf.Interface(config.primary_soc.jlink_snr)
        hardware_id = interface.unique_device_id()
        hardware_id_str = f"{hardware_id:0{2 * interface.unique_device_id_len}x}"
    except Exception as e:
        test.logger.error(e)
        return PhaseResult.STOP

    test.measurements.hardware_id = hardware_id_str
    test.logger.info(f"Hardware ID: {hardware_id_str}")
    test.state["primary_interface"] = interface
    test.state["hardware_id"] = hardware_id_str
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Recover Device")
def recover_dut(test: openhtf.TestApi, config: ValidationConfig):
    """Recover the DUT cores using nrfutil."""

    for soc_name, soc_config in config.socs.items():
        if not soc_config.recover:
            continue

        RECOVER_CMD = [
            "nrfutil",
            "device",
            "recover",
            "--serial-number",
            str(soc_config.jlink_snr),
        ]

        test.logger.info(f"Recovering DUT SoC '{soc_name}' with: {' '.join(RECOVER_CMD)}")
        result = subprocess.run(RECOVER_CMD, capture_output=True, text=True)
        if result.returncode != 0:
            test.logger.error("❌ Recovery failed:\n%s", result.stderr)
            return PhaseResult.STOP

    test.logger.info("✅ Recovery successful.")
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Infuse-IoT Provision Device")
def provision_dut(test: openhtf.TestApi, config: ValidationConfig):
    """Provision device in Infuse-IoT"""

    interface = test.state["primary_interface"]
    hardware_id = test.state["hardware_id"]

    provision_info = config.infuse.provision(interface, hardware_id)
    device_id = int(provision_info.device_id, 16)
    test.state["provisioning_info"] = provision_info
    test.state["provisioning_struct"] = nrf.Interface.DefaultProvisioningStruct(device_id)


@openhtf.PhaseOptions(name="Program nRF9151 Modem")
def nrf91_modem_program(test: openhtf.TestApi, config: ValidationConfig):
    """Program the nRF91 modem core using nrfutil"""

    socs = [soc for soc in config.socs.values() if soc.nrf91_modem_firmware]
    # Loading stage should filter out other lengths
    assert len(socs) == 1
    modem_soc = socs[0]

    # Construct flashing command
    FLASH_CMD = [
        "nrfutil",
        "91",
        "modem-firmware-upgrade",
        "--firmware",
        modem_soc.nrf91_modem_firmware,
        "--verify",
        "--serial-number",
        str(modem_soc.jlink_snr),
    ]

    test.logger.info(f"Flashing nRF91 modem with: {' '.join(FLASH_CMD)}")
    result = subprocess.run(FLASH_CMD, capture_output=True, text=True)
    if result.returncode != 0:
        test.logger.error("❌ nRF91 Modem Flash failed:\n%s", result.stderr)
        return PhaseResult.STOP
    test.logger.info("✅ nRF91 Modem Flash Flash successful.")
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Flash {phase} binaries")
def flash_binaries(test: openhtf.TestApi, config: ValidationConfig, phase: str):
    """Flash the DUT cores using nrfutil."""

    for soc_name, soc_config in config.socs.items():
        file_info = soc_config.binaries.get(phase)
        if file_info is None:
            continue

        flash_file = file_info.file
        if file_info.provision:
            # Create a temporary file for the merged hex
            with tempfile.NamedTemporaryFile(suffix=".hex", delete=False) as tmp:
                interface: nrf.Interface = test.state["primary_interface"]
                struct: nrf.Interface.DefaultProvisioningStruct = test.state["provisioning_struct"]
                customer_addr = interface.uicr_base + interface.family.CUSTOMER_OFFSET
                file_info.hex_append(customer_addr, bytes(struct), tmp.name)
                flash_file = tmp.name

        # Construct flashing command
        FLASH_CMD = [
            "nrfutil",
            "device",
            "program",
            "--firmware",
            flash_file,
            "--serial-number",
            str(soc_config.jlink_snr),
        ]
        FLASH_CMD += file_info.nrfutil_option_args()

        test.logger.info(f"Flashing DUT SoC '{soc_name}' with: {' '.join(FLASH_CMD)}")
        result = subprocess.run(FLASH_CMD, capture_output=True, text=True)

        # Cleanup temporary file if created
        if file_info.provision:
            os.remove(flash_file)

        if result.returncode != 0:
            test.logger.error("❌ Flash failed:\n%s", result.stderr)
            return PhaseResult.STOP

    test.logger.info("✅ Flash successful.")
    return PhaseResult.CONTINUE


def line_handler(test: openhtf.TestApi, config: ValidationConfig, line: str):
    match = config.result_pattern.match(line)
    if not match:
        return

    _timestamp, subsys, msg_type, value = match.groups()
    meas_name = subsys
    try:
        if msg_type == "PASS":
            setattr(test.measurements, subsys, Outcome.PASS.value)
        elif msg_type == "ERROR":
            setattr(test.measurements, subsys, Outcome.FAIL.value)
        elif msg_type == "VAL":
            subtest, val = config.val_pattern.match(value).groups()
            meas_name = f"{subsys}.{subtest}"
            if subsys not in config.tests or subtest not in config.tests[subsys].subtests:
                raise NotAMeasurementError(meas_name)
            cfg = config.tests[subsys].subtests[subtest]
            setattr(test.measurements, meas_name, cfg.convert(val))

    except NotAMeasurementError as _e:
        test.logger.warning(f"{meas_name} not listed in configuration file")


def line_reader(test: openhtf.TestApi, port: SerialLike) -> Generator[str, None, None]:
    end_time = time.time() + CONF.general["test-timeout"]
    line = ""
    while time.time() < end_time:
        if test.get_measurement("SYS").outcome != Outcome.UNSET:
            break

        try:
            # Read bytes from serial port
            rx = port.read_bytes(1024)
        except Exception as e:
            test.logger.warning("Port read error: %s", e)
            time.sleep(0.1)
            continue

        if len(rx) == 0:
            time.sleep(0.05)
            continue
        for b in rx:
            c = chr(b)
            if c == "\n":
                yield line.strip()
                line = ""
            if c != "\n":
                line += c


@openhtf.PhaseOptions(name="Hardware Validation")
def validate_dut(test: openhtf.TestApi, config: ValidationConfig):
    """Connect to serial data port and parse results."""
    test.logger.info("Connecting to DUT...")

    # Open the port
    port = config.primary_soc.serial_port
    try:
        # TF-M can take a substantial time to boot after the first program
        port.open(20.0)
    except Exception as e:
        test.logger.error(f"Serial port open failed: {e}")
        return PhaseResult.STOP

    # Read lines from the serial port
    for line in line_reader(test, port):
        test.logger.debug("Port: %s", line)
        line_handler(test, config, line)

    # Close the serial port
    port.close()

    # Examine measurements to determine result
    for measure in test.measurements._measurements.values():
        if measure.outcome != Outcome.PASS:
            test.logger.error("❌ Validation failed.")
            return PhaseResult.STOP
    test.logger.info("✅ Validation passed.")
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Get Operator ID", timeout_s=ONE_DAY)
@plugs.plug(prompts=UserInput)
def operator_id_query(test: openhtf.TestApi, prompts: UserInput) -> None:
    """Test start trigger that prompts the user for an Operator ID."""
    text = prompts.prompt(
        "Enter an Operator ID in order to start testing",
        text_input=True,
        image_url="https://www.tofupilot.com/logo.svg",
        timeout_s=ONE_DAY,
        cli_color="",
    )

    if not re.match(f"^{CONF.general['valid-operator-regex']}$", text):
        test.logger.error(f"Invalid Operator ID '{text}'")
        return PhaseResult.STOP

    global operator_id
    operator_id = text
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Serial Number User Entry", timeout_s=ONE_DAY)
@plugs.plug(prompts=UserInput)
def dut_id_query(test: openhtf.TestApi, prompts: UserInput, config: ValidationConfig) -> None:
    """Test start trigger that prompts the user for a DUT ID."""
    dut_id = prompts.prompt(
        "Enter a DUT ID in order to start the test",
        text_input=True,
        timeout_s=ONE_DAY,
        cli_color="",
    )

    if not re.match(f"^{CONF.general['valid-dut-regex']}$", dut_id):
        test.logger.error(f"Invalid DUT ID '{dut_id}'")
        return PhaseResult.STOP

    # Insert operator ID into the test record so its saved to output logs
    if config.operator is not None:
        test.logger.info(f"Operator ID: '{config.operator}'")

    test.test_record.dut_id = dut_id
    return PhaseResult.CONTINUE


@openhtf.PhaseOptions(name="Notify Operator", timeout_s=ONE_DAY)
@plugs.plug(prompts=UserInput)
def operator_ack_result(test: openhtf.TestApi, prompts: UserInput):
    """Test start trigger that prompts the user for a DUT ID."""

    all_passed = all(p.outcome == test_record.PhaseOutcome.PASS for p in test.test_record.phases)
    image_conf = CONF.general["images"]
    if all_passed:
        image_url = f"{image_conf['url-base']}{image_conf['pass']}"
        message = "DUT passed, confirm result"
    else:
        image_url = f"{image_conf['url-base']}{image_conf['fail']}"
        message = "DUT failed, confirm result"

    prompts.prompt(
        message,
        image_url=image_url,
        timeout_s=ONE_DAY,
    )
    return PhaseResult.CONTINUE
