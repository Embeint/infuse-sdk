# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import pathlib
import subprocess
import yaml
import sys
from http import HTTPStatus

from infuse_iot.api_client import Client
from infuse_iot.credentials import get_api_key
from infuse_iot.util.soc import nrf
from infuse_iot.api_client.api.device import (
    get_device_by_soc_and_mcu_id,
)
from infuse_iot.api_client.models import Device

from west.commands import WestCommand


EXPORT_DESCRIPTION = """\
This command flashes an application release.
"""


class release_flash(WestCommand):
    def __init__(self):
        super().__init__(
            "release-flash",
            # Keep this in sync with the string in west-commands.yml.
            "Flash an Infuse-IoT application release",
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
        )

        parser.add_argument(
            "-r",
            "--release",
            type=pathlib.Path,
            required=True,
            help="Application release folder",
        )
        parser.add_argument(
            "--erase",
            action="store_true",
            help="Provide --erase argument to 'west flash'",
        )
        parser.add_argument(
            "--reprovision",
            action="store_true",
            help="Recover and provision the device as part of flashing",
        )
        parser.add_argument(
            "--jlink",
            type=int,
            help="JLink serial number",
        )
        return parser

    def nrf_recover(self, jlink: int | None):
        recover_cmd = ["nrfutil", "device", "recover"]
        if jlink:
            recover_cmd += ["--serial-number", str(jlink)]
        print("Run recover: ", " ".join(recover_cmd))
        subprocess.run(recover_cmd, check=True)

    def infuse_provisioning_info(self, interface: nrf.Interface) -> Device:
        client = Client(base_url="https://api.infuse-iot.com").with_headers(
            {"x-api-key": f"Bearer {get_api_key()}"}
        )
        hardware_id = interface.unique_device_id()
        hardware_id_str = f"{hardware_id:0{2 * interface.unique_device_id_len}x}"

        with client as client:
            response = get_device_by_soc_and_mcu_id.sync_detailed(
                client=client, soc=interface.soc_name, mcu_id=hardware_id_str
            )
            if response.status_code != HTTPStatus.OK:
                sys.exit(
                    f"Failed to query device:\n\t<{response.status_code}> {response.content.decode('utf-8')}"
                )
            assert isinstance(response.parsed, Device)
            return response.parsed

    def do_run(self, args, _unknown_args):
        manifest_file = args.release / "manifest.yaml"
        with manifest_file.open("r", encoding="utf-8") as f:
            manifest = yaml.safe_load(f)

        provisioning_bytes: bytes | None = None
        if args.reprovision:
            if not manifest["application"]["soc"].startswith("nrf"):
                sys.exit("'--reprovision' currently only supported for Nordic SoCs")
            # Recover the SoC
            self.nrf_recover(args.jlink)
            # Get the hardware ID
            interface = nrf.Interface(args.jlink)
            # Get provisioning info from Infuse-IoT
            device_info = self.infuse_provisioning_info(interface)
            infuse_id = int(device_info.device_id, 16)
            provisioning_bytes = bytes(interface.DefaultProvisioningStruct(infuse_id))
            # Write the provisioning information to the SoC
            interface.write_provisioning_data(provisioning_bytes)
            print(
                f"HW ID 0x{device_info.mcu_id} now provisioned as 0x{device_info.device_id}"
            )

        flash_cmd = [
            "west",
            "flash",
            "--skip-rebuild",
            "-d",
            str(args.release / manifest["application"]["primary"]),
            "--hex-file",
            str(args.release / manifest["application"]["hex"]),
        ]
        if args.erase:
            flash_cmd.append("--erase")
        if args.reprovision:
            flash_cmd.append("--dev-id")
            flash_cmd.append(str(args.jlink))

        print("Run flash: ", " ".join(flash_cmd))
        subprocess.run(flash_cmd, check=True)
