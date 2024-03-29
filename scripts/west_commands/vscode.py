# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Pty Ltd

import argparse
import shutil
import pathlib
import json

from west.commands import WestCommand
from west.util import west_topdir
from west import log

EXPORT_DESCRIPTION = '''\
This command generates default VSCode configuration files for
use with the Embeint SDK. The e

This command registers the current Zephyr installation as a CMake
config package in the CMake user package registry.

In Windows, the CMake user package registry is found in:
HKEY_CURRENT_USER\\Software\\Kitware\\CMake\\Packages\\

In Linux and MacOS, the CMake user package registry is found in:
~/.cmake/packages/'''

settings = {
    "files.trimFinalNewlines": True,
    "files.insertFinalNewline": True,
    "editor.trimAutoWhitespace": True,
    "editor.formatOnSave": True,
    "editor.defaultFormatter": "ms-vscode.cpptools",
    "C_Cpp.clang_format_style": "file:${workspaceFolder}/embeint-sdk/.clang-format",
}

recommended_extensions = {
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "marus25.cortex-debug",
        "nordic-semiconductor.nrf-devicetree",
        "nordic-semiconductor.nrf-kconfig",
    ]
}

class vscode(WestCommand):

    def __init__(self):
        super().__init__(
            'vscode',
            # Keep this in sync with the string in west-commands.yml.
            'generate Visual Studio code configuration',
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description)
        print(type(parser_adder))
        parser.add_argument('--workspace', type=str, default=west_topdir(), help='VSCode workspace folder')
        return parser

    def do_run(self, args, unknown_args):
        settings['python.defaultInterpreterPath'] = shutil.which('python3')
        vscode_folder = pathlib.Path(args.workspace) / '.vscode'

        log.inf(f"Writing `settings.json` and `extensions.json` to {vscode_folder}")
        with (vscode_folder / 'settings.json').open('w') as f:
            json.dump(settings, f, indent=4)
        with (vscode_folder / 'extensions.json').open('w') as f:
            json.dump(recommended_extensions, f, indent=4)
