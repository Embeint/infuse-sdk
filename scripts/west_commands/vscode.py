# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Pty Ltd

import argparse
import shutil
import pathlib
import json
import subprocess
import sys

from west.commands import WestCommand
from west.util import west_topdir
from west import log

import zcmake

EXPORT_DESCRIPTION = '''\
This command generates default VSCode configuration files for
use with the Embeint SDK.
'''

settings = {
    "files.trimTrailingWhitespace": True,
    "files.trimFinalNewlines": True,
    "files.insertFinalNewline": True,
    "editor.tabCompletion": "on",
    "editor.trimAutoWhitespace": True,
    "editor.formatOnSave": True,
    "editor.defaultFormatter": "ms-vscode.cpptools",
    "C_Cpp.clang_format_style": "file:${workspaceFolder}/embeint-sdk/.clang-format",
    "[cmake]": {
        "editor.insertSpaces": True,
        "editor.tabSize": 2,
        "editor.indentSize": "tabSize"
    },
    "[c]": {
        "editor.insertSpaces": False,
        "editor.tabSize": 8,
        "editor.indentSize": "tabSize"
    }
}

recommended_extensions = {
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "marus25.cortex-debug",
        "nordic-semiconductor.nrf-devicetree",
        "nordic-semiconductor.nrf-kconfig",
        "cschlosser.doxdocgen",
    ]
}

file_snippets = {
	"new_file_c": {
        "prefix": "new_file_c",
		"scope": "c,cpp",
		"description": "Generate file header for C source file",
		"body": []
	},
    "new_file_h": {
        "prefix": "new_file_h",
		"scope": "c,cpp",
		"description": "Generate file header for C header file",
		"body": []
	},
}

def author():
    def git_config(config):
        proc = subprocess.run(["git", "config", config], stdout=subprocess.PIPE)
        return proc.stdout.strip().decode()

    return f" * @author {git_config('user.name')} <{git_config('user.email')}>"

def c_source_header():
    return """/**
 * @file
 * @copyright $CURRENT_YEAR Embeint Pty Ltd
""" + author() + """
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */
"""

def c_header_header():
    return """/**
 * @file
 * @brief $1
 * @copyright $CURRENT_YEAR Embeint Pty Ltd
""" + author() + """
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details $2
 */

#ifndef ${RELATIVE_FILEPATH/(?:^.*\\\\src\\\\)?(\\w+)(?!\\w*$)|(\\W)|(\\w+)$/${1:/upcase}${2:+_}${3:/upcase}${3:+_}/g}
#define ${RELATIVE_FILEPATH/(?:^.*\\\\src\\\\)?(\\w+)(?!\\w*$)|(\\W)|(\\w+)$/${1:/upcase}${2:+_}${3:/upcase}${3:+_}/g}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ${TM_FILENAME_BASE} API
 * @defgroup ${TM_FILENAME_BASE:/downcase}_apis ${TM_FILENAME_BASE} APIs
 * @{
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ${RELATIVE_FILEPATH/(?:^.*\\\\src\\\\)?(\\w+)(?!\\w*$)|(\\W)|(\\w+)$/${1:/upcase}${2:+_}${3:/upcase}${3:+_}/g} */
"""

c_cpp_properties = {
    "configurations": [
        {
            "name": "ECS",
            "configurationProvider": "ms-vscode.cmake-tools",
            "compilerPath": "",
            "compileCommands": "",
            "includePath": ""
        }
    ],
    "version": 4
}

launch = {
    "configurations": [
        {
            "name": "Attach",
            "type": "cortex-debug",
            "request": "attach",
            "servertype": "jlink",
            "rtos": "Zephyr",
            "gdbPath": "",
            "device": "",
            "executable": "",
        },
        {
            "name": "Launch",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "jlink",
            "rtos": "Zephyr",
            "gdbPath": "",
            "device": "",
            "executable": "",
        },
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
        parser.add_argument('--workspace', type=str, default=west_topdir(), help='VSCode workspace folder')
        parser.add_argument('--dir', '-d', type=str, help='Application build folder')
        return parser

    def do_run(self, args, unknown_args):
        vscode_folder = pathlib.Path(args.workspace) / '.vscode'

        if args.dir is None:
            log.inf(f"Writing `settings.json`, `extensions.json` and `eis.code-snippets` to {vscode_folder}")

            settings['python.defaultInterpreterPath'] = shutil.which('python3')
            file_snippets['new_file_c']['body'] = c_source_header().splitlines()
            file_snippets['new_file_h']['body'] = c_header_header().splitlines()

            with (vscode_folder / 'settings.json').open('w') as f:
                json.dump(settings, f, indent=4)
            with (vscode_folder / 'extensions.json').open('w') as f:
                json.dump(recommended_extensions, f, indent=4)
            with (vscode_folder / 'eis.code-snippets').open('w') as f:
                json.dump(file_snippets, f, indent=4)
        else:
            dir = pathlib.Path(args.dir).absolute().resolve()

            if not (dir / 'CMakeCache.txt').exists():
                log.err(f"{args.dir} does not appear to be a valid cmake build directory")
                sys.exit(1)

            cache = zcmake.CMakeCache.from_build_dir(dir)

            c_cpp_properties['configurations'][0]['includePath'] = [str(dir / 'zephyr' / 'include' / 'generated')]
            c_cpp_properties['configurations'][0]['compileCommands'] = str(dir / 'compile_commands.json')
            c_cpp_properties['configurations'][0]['compilerPath'] = cache.get('CMAKE_C_COMPILER')

            launch['configurations'][0]['executable'] = str(dir / 'zephyr' / 'zephyr.elf')
            launch['configurations'][1]['executable'] = str(dir / 'zephyr' / 'zephyr.elf')
            launch['configurations'][0]['device'] = "NRF5340_XXAA_APP"
            launch['configurations'][1]['device'] = "NRF5340_XXAA_APP"
            launch['configurations'][0]['gdbPath'] = cache.get('CMAKE_GDB')
            launch['configurations'][1]['gdbPath'] = cache.get('CMAKE_GDB')

            if cache.get('BOARD')[-3:] == '_ns':
                # Add TF-M .elf files
                launch['configurations'][0]['preAttachCommands'] = [
                    f"add-symbol-file {str(dir)}/tfm/bin/bl2.elf",
                    f"add-symbol-file {str(dir)}/tfm/bin/tfm_s.elf",
                ]
            log.inf(f"Writing `c_cpp_properties.json` and `launch.json` to {vscode_folder}")

            with (vscode_folder / 'c_cpp_properties.json').open('w') as f:
                json.dump(c_cpp_properties, f, indent=4)
            with (vscode_folder / 'launch.json').open('w') as f:
                json.dump(launch, f, indent=4)
