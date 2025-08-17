# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import pathlib
import subprocess
import yaml

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
        return parser

    def do_run(self, args, _unknown_args):
        manifest_file = args.release / "manifest.yaml"
        with manifest_file.open("r", encoding="utf-8") as f:
            manifest = yaml.safe_load(f)

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

        print("Run flash: ", " ".join(flash_cmd))
        subprocess.run(flash_cmd, check=True)
