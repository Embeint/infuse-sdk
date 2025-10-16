# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Embeint Holdings Pty Ltd

import argparse
import pathlib
import json
import yaml

from west.commands import WestCommand
from west.util import west_topdir
from west import log

import zcmake

EXPORT_DESCRIPTION = """\
This command generates default Zed configuration files for
use with the Embeint SDK.
"""


class zed(WestCommand):
    def __init__(self):
        super().__init__(
            "zed",
            # Keep this in sync with the string in west-commands.yml.
            "generate Zed configuration",
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
            "--workspace",
            type=str,
            default=west_topdir(),
            help="VSCode workspace folder",
        )
        parser.add_argument(
            "--build-dir",
            "-d",
            dest="dir",
            type=str,
            required=True,
            help="Application build folder",
        )
        return parser

    def do_run(self, args, _):
        zed_folder = pathlib.Path(args.workspace) / ".zed"
        zed_folder.mkdir(exist_ok=True)

        workspace_dir = pathlib.Path(args.workspace).absolute().resolve()
        build_dir = pathlib.Path(args.dir).absolute().resolve()
        cache = zcmake.CMakeCache.from_build_dir(build_dir)
        runners_yaml = None
        if build_dir.parts[-1] == "tfm":
            runners_yaml_path = build_dir / ".." / "zephyr" / "runners.yaml"
            cache = zcmake.CMakeCache.from_build_dir(build_dir.parent)
        else:
            runners_yaml_path = build_dir / "zephyr" / "runners.yaml"
        if runners_yaml_path.exists():
            with pathlib.Path(runners_yaml_path).open("r", encoding="utf-8") as f:
                runners_yaml = yaml.safe_load(f)

        clangd_args = [
            f"--compile-commands-dir={build_dir}",
            "--enable-config",
        ]
        if runners_yaml is None:
            pass
        elif runners_yaml["debug-runner"] == "jlink":
            compiler_folder = pathlib.Path(cache.get("CMAKE_GDB")).parent
            clangd_args.append(f"--query-driver={compiler_folder}/**")

        settings = {
            "lsp": {
                "clangd": {
                    "binary": {
                        "arguments": clangd_args,
                    }
                }
            },
            "languages": {
                "C": {
                    "formatter": {
                        "external": {
                            "command": "clang-format",
                            "arguments": [
                                f"--style=file:{workspace_dir}/infuse-sdk/.clang-format",
                                "--assume-filename={buffer_path}",
                            ],
                        }
                    },
                    "format_on_save": "on",
                    "tab_size": 8,
                }
            },
            "file_types": {
                "C": ["h"],
            },
        }

        log.inf(f"Writing `settings.json` to {zed_folder}")

        with (zed_folder / "settings.json").open("w") as f:
            json.dump(settings, f, indent=4)
