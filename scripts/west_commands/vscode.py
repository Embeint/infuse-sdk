# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Inc

import argparse
import shutil
import pathlib
import json
import subprocess
import sys
import yaml

from west.commands import WestCommand
from west.util import west_topdir
from west import log

import zcmake

EXPORT_DESCRIPTION = """\
This command generates default VSCode configuration files for
use with the Embeint SDK.
"""

settings = {
    "files.trimTrailingWhitespace": True,
    "files.trimFinalNewlines": True,
    "files.insertFinalNewline": True,
    "editor.tabCompletion": "on",
    "editor.trimAutoWhitespace": True,
    "editor.formatOnSave": True,
    "editor.defaultFormatter": "ms-vscode.cpptools",
    "C_Cpp.clang_format_style": "file:${workspaceFolder}/infuse-sdk/.clang-format",
    "[cmake]": {
        "editor.insertSpaces": True,
        "editor.tabSize": 2,
        "editor.indentSize": "tabSize",
    },
    "[c]": {
        "editor.insertSpaces": False,
        "editor.tabSize": 8,
        "editor.indentSize": "tabSize",
    },
    "[dts]": {
        "editor.insertSpaces": False,
        "editor.tabSize": 8,
        "editor.indentSize": "tabSize",
    },
    "[kconfig]": {
        "editor.insertSpaces": False,
        "editor.tabSize": 4,
        "editor.indentSize": "tabSize",
    },
    "[jinja]": {"editor.formatOnSave": False},
    "[python]": {"editor.defaultFormatter": "ms-python.black-formatter"},
}

recommended_extensions = {
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "ms-python.black-formatter",
        "ms-python.pylint",
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
        "body": [],
    },
    "new_file_h": {
        "prefix": "new_file_h",
        "scope": "c,cpp",
        "description": "Generate file header for C header file",
        "body": [],
    },
}


def author():
    """Get current git user configuration"""

    def git_config(config):
        proc = subprocess.run(
            ["git", "config", config], stdout=subprocess.PIPE, check=False
        )
        return proc.stdout.strip().decode()

    return f" * @author {git_config('user.name')} <{git_config('user.email')}>"


def c_source_header():
    """File header for C source files"""
    return (
        """/**
 * @file
 * @copyright $CURRENT_YEAR Embeint Inc
"""
        + author()
        + """
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */
"""
    )


def c_header_header():
    """File header for C header files"""
    return (
        """/**
 * @file
 * @brief $1
 * @copyright $CURRENT_YEAR Embeint Inc
"""
        + author()
        + """
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
    )


c_cpp_properties = {
    "configurations": [
        {
            "name": "ECS",
            "configurationProvider": "ms-vscode.cmake-tools",
            "compilerPath": "",
            "compileCommands": "",
            "includePath": "",
        }
    ],
    "version": 4,
}

launch = {
    "configurations": [
        {
            "name": "Attach",
            "type": "cortex-debug",
            "request": "attach",
            "executable": "",
        },
        {
            "name": "Launch",
            "type": "cortex-debug",
            "request": "launch",
            "executable": "",
        },
    ]
}


class vscode(WestCommand):
    def __init__(self):
        super().__init__(
            "vscode",
            # Keep this in sync with the string in west-commands.yml.
            "generate Visual Studio code configuration",
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
        parser.add_argument("--dir", "-d", type=str, help="Application build folder")
        return parser

    def _jlink_device(self, cache):
        # Get the JLink device name
        runners_yaml = cache.get("ZEPHYR_RUNNERS_YAML")
        if runners_yaml is not None:
            with pathlib.Path(runners_yaml).open("r", encoding="utf-8") as f:
                r = yaml.safe_load(f)
                if "jlink" in r["args"]:
                    for arg in r["args"]["jlink"]:
                        if arg.startswith("--device="):
                            device = arg.removeprefix("--device=")
                            launch["configurations"][0]["device"] = device
                            launch["configurations"][1]["device"] = device
                            break

    def _tfm_build(self, build_dir, cache):
        launch["configurations"][0]["executable"] = str(build_dir / "bin" / "tfm_s.elf")
        launch["configurations"][1]["executable"] = str(build_dir / "bin" / "tfm_s.elf")

        # Get options from parent Zephyr build
        parent_cache = zcmake.CMakeCache.from_build_dir(build_dir.parent)
        c_cpp_properties["configurations"][0]["compilerPath"] = parent_cache.get(
            "CMAKE_C_COMPILER"
        )
        launch["configurations"][0]["gdbPath"] = parent_cache.get("CMAKE_GDB")
        launch["configurations"][1]["gdbPath"] = parent_cache.get("CMAKE_GDB")

        launch["configurations"][0]["servertype"] = "jlink"
        launch["configurations"][1]["servertype"] = "jlink"
        self._jlink_device(parent_cache)

    def _zephyr_build(self, build_dir, cache):
        c_cpp_properties["configurations"][0]["includePath"] = [
            str(build_dir / "zephyr" / "include" / "generated")
        ]
        c_cpp_properties["configurations"][0]["compilerPath"] = cache.get(
            "CMAKE_C_COMPILER"
        )

        launch["configurations"][0]["gdbPath"] = cache.get("CMAKE_GDB")
        launch["configurations"][1]["gdbPath"] = cache.get("CMAKE_GDB")
        if cache.get("SOC_SVD_FILE", False):
            launch["configurations"][0]["svdFile"] = cache.get("SOC_SVD_FILE")
            launch["configurations"][1]["svdFile"] = cache.get("SOC_SVD_FILE")

        launch["configurations"][0]["executable"] = str(
            build_dir / "zephyr" / "zephyr.elf"
        )
        launch["configurations"][1]["executable"] = str(
            build_dir / "zephyr" / "zephyr.elf"
        )

        if cache.get("BOARD")[-3:] == "_ns":
            tfm_elfs = [
                "bl2.elf",
                "tfm_s.elf",
            ]
            tfm_paths = [build_dir / "tfm" / "bin" / elf for elf in tfm_elfs]
            tfm_exists = [p for p in tfm_paths if p.exists()]

            # Add TF-M .elf files
            launch["configurations"][0]["preAttachCommands"] = [
                f"add-symbol-file {str(path)}" for path in tfm_exists
            ]

        if cache.get("QEMU", False):
            launch["configurations"][0]["name"] = "QEMU Attach"
            launch["configurations"][0]["servertype"] = "external"
            launch["configurations"][0]["gdbTarget"] = "localhost:1234"
            launch["configurations"][0]["serverpath"] = cache.get("QEMU")
            launch["configurations"][0]["runToEntryPoint"] = False

            launch["configurations"][1]["name"] = "QEMU Launch"
            launch["configurations"][1]["servertype"] = "qemu"
            launch["configurations"][1]["serverpath"] = cache.get("QEMU")
            launch["configurations"][1]["runToEntryPoint"] = False
        elif cache.get("BOARD")[:10] == "native_sim":
            # Native Sim GDB does not support `west debugserver`
            launch["configurations"].pop(0)

            launch["configurations"][0]["name"] = "Native Launch"
            launch["configurations"][0]["type"] = "cppdbg"
            launch["configurations"][0]["program"] = str(
                build_dir / "zephyr" / "zephyr.exe"
            )
            launch["configurations"][0]["cwd"] = str(build_dir)
        else:
            launch["configurations"][0]["rtos"] = "Zephyr"
            launch["configurations"][1]["rtos"] = "Zephyr"
            launch["configurations"][0]["servertype"] = "jlink"
            launch["configurations"][1]["servertype"] = "jlink"

        self._jlink_device(cache)

    def do_run(self, args, _):
        vscode_folder = pathlib.Path(args.workspace) / ".vscode"
        vscode_folder.mkdir(exist_ok=True)

        if args.dir is None:
            log.inf(
                f"Writing `settings.json`, `extensions.json` and `infuse.code-snippets` to {vscode_folder}"
            )

            settings["python.defaultInterpreterPath"] = shutil.which("python3")
            file_snippets["new_file_c"]["body"] = c_source_header().splitlines()
            file_snippets["new_file_h"]["body"] = c_header_header().splitlines()

            with (vscode_folder / "settings.json").open("w") as f:
                json.dump(settings, f, indent=4)
            with (vscode_folder / "extensions.json").open("w") as f:
                json.dump(recommended_extensions, f, indent=4)
            with (vscode_folder / "infuse.code-snippets").open("w") as f:
                json.dump(file_snippets, f, indent=4)
        else:
            build_dir = pathlib.Path(args.dir).absolute().resolve()

            if not (build_dir / "CMakeCache.txt").exists():
                log.err(
                    f"{args.build_dir} does not appear to be a valid cmake build directory"
                )
                sys.exit(1)

            cache = zcmake.CMakeCache.from_build_dir(build_dir)

            c_cpp_properties["configurations"][0]["compileCommands"] = str(
                build_dir / "compile_commands.json"
            )

            if build_dir.parts[-1] == "tfm":
                self._tfm_build(build_dir, cache)
            else:
                self._zephyr_build(build_dir, cache)

            log.inf(
                f"Writing `c_cpp_properties.json` and `launch.json` to {vscode_folder}"
            )

            with (vscode_folder / "c_cpp_properties.json").open("w") as f:
                json.dump(c_cpp_properties, f, indent=4)
            with (vscode_folder / "launch.json").open("w") as f:
                json.dump(launch, f, indent=4)
