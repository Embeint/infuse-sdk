# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import shutil
import pathlib
import json
import subprocess
import sys
import yaml
import pylink

try:
    from simple_term_menu import TerminalMenu
except NotImplementedError:
    pass

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
    "scm.showHistoryGraph": False,
    "editor.tabCompletion": "on",
    "editor.trimAutoWhitespace": True,
    "editor.formatOnSave": True,
    "editor.defaultFormatter": "ms-vscode.cpptools",
    "C_Cpp.clang_format_style": "file:${workspaceFolder}/infuse-sdk/.clang-format",
    "search.exclude": {
        "**/*.a": True,
        "**/*.o": True,
        "**/*.obj": True,
        "**/*.gcda": True,
        "**/*.gcno": True,
    },
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
    "[python]": {"editor.defaultFormatter": "charliermarsh.ruff"},
}

recommended_extensions = {
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "charliermarsh.ruff",
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
 * @copyright $CURRENT_YEAR Embeint Holdings Pty Ltd
"""
        + author()
        + """
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */
"""
    )


def c_header_header():
    """File header for C header files"""
    return (
        """/**
 * @file
 * @brief $1
 * @copyright $CURRENT_YEAR Embeint Holdings Pty Ltd
"""
        + author()
        + """
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
            "name": "Infuse-IoT",
            "configurationProvider": "ms-vscode.cmake-tools",
            "compilerPath": "",
            "compileCommands": "",
            "includePath": [],
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
        parser.add_argument(
            "--search-exclude",
            action="store_true",
            help="Exclude common build folders from search paths",
        )
        parser.add_argument(
            "--build-dir", "-d", dest="dir", type=str, help="Application build folder"
        )
        parser.add_argument("--snr", type=str, help="JTAG serial number")
        return parser

    def cpp_properties(self, build_dir, cache, include_path=True):
        c_cpp_properties["configurations"][0]["compilerPath"] = cache.get(
            "CMAKE_C_COMPILER"
        )
        if include_path:
            c_cpp_properties["configurations"][0]["includePath"] = [
                str(build_dir / "zephyr" / "include" / "generated")
            ]

    def _tfm_build(self, build_dir, cache):
        launch["configurations"][0]["executable"] = str(build_dir / "bin" / "tfm_s.elf")
        launch["configurations"][1]["executable"] = str(build_dir / "bin" / "tfm_s.elf")

        bl2_elf = build_dir / "bin" / "bl2.elf"
        if bl2_elf.exists():
            launch["configurations"][0]["preAttachCommands"] = [
                f"add-symbol-file {bl2_elf}"
            ]

        # Get options from parent Zephyr build
        parent_cache = zcmake.CMakeCache.from_build_dir(build_dir.parent)
        self.cpp_properties(build_dir, parent_cache, False)
        launch["configurations"][0]["gdbPath"] = parent_cache.get("CMAKE_GDB")
        launch["configurations"][1]["gdbPath"] = parent_cache.get("CMAKE_GDB")

    def _zephyr_build(self, build_dir, cache):
        self.cpp_properties(build_dir, cache)

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

        if cache.get("BOARD")[-3:] == "/ns":
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
        if cache.get("SYSBUILD", False):
            # Check if a `mcuboot` folder exists at the same level
            mcuboot_elf = build_dir / ".." / "mcuboot" / "zephyr" / "zephyr.elf"
            if mcuboot_elf.exists():
                # Add the mcuboot .elf file
                launch["configurations"][0]["preAttachCommands"] = [
                    f"add-symbol-file {str(mcuboot_elf.resolve())}"
                ]

    def _qemu(self, build_dir, cache):
        assert cache.get("QEMU", False)

        self.cpp_properties(build_dir, cache)

        launch["configurations"][0]["name"] = "QEMU Attach"
        launch["configurations"][0]["servertype"] = "external"
        launch["configurations"][0]["gdbTarget"] = "localhost:1234"
        launch["configurations"][0]["serverpath"] = cache.get("QEMU")
        launch["configurations"][0]["gdbPath"] = cache.get("CMAKE_GDB")
        launch["configurations"][0]["runToEntryPoint"] = False
        launch["configurations"][0]["executable"] = str(
            build_dir / "zephyr" / "zephyr.elf"
        )

        launch["configurations"][1]["name"] = "QEMU Launch"
        launch["configurations"][1]["servertype"] = "qemu"
        launch["configurations"][1]["serverpath"] = cache.get("QEMU")
        launch["configurations"][1]["gdbPath"] = cache.get("CMAKE_GDB")
        launch["configurations"][1]["runToEntryPoint"] = False
        launch["configurations"][1]["executable"] = str(
            build_dir / "zephyr" / "zephyr.elf"
        )

    def _native(self, build_dir, cache):
        assert cache.get("BOARD")[:10] in [
            "native_sim",
            "nrf52_bsim",
            "unit_testi",
        ]

        self.cpp_properties(build_dir, cache)

        # Native Sim GDB does not support `west debugserver`
        launch["configurations"].pop(0)

        launch["configurations"][0].pop("executable")
        launch["configurations"][0]["name"] = "Native Launch"
        launch["configurations"][0]["type"] = "cppdbg"
        launch["configurations"][0]["program"] = str(
            build_dir / "zephyr" / "zephyr.exe"
        )
        launch["configurations"][0]["cwd"] = str(build_dir)

        if cache.get("BOARD")[:10] == "nrf52_bsim":
            # Template likely arguments
            launch["configurations"][0]["args"] = [
                "-s=sim_id",
                "-d=0",
                "-RealEncryption=0",
                "-testid=test_id",
            ]

    def _jlink_device_name(self, runners_yaml):
        for arg in runners_yaml["args"]["jlink"]:
            if arg.startswith("--device="):
                return arg.removeprefix("--device=")
        return None

    def _jlink_device(self, runners_yaml):
        if device := self._jlink_device_name(runners_yaml):
            launch["configurations"][0]["device"] = device
            launch["configurations"][1]["device"] = device
        launch["configurations"][0]["servertype"] = "jlink"
        launch["configurations"][1]["servertype"] = "jlink"
        launch["configurations"][0]["rtos"] = "Zephyr"
        launch["configurations"][1]["rtos"] = "Zephyr"

    def _jlink(self, snr, build_dir, cache, runners_yaml):
        self._jlink_device(runners_yaml)
        if snr is not None:
            launch["configurations"][0]["serialNumber"] = snr
            launch["configurations"][1]["serialNumber"] = snr
            return

        try:
            jlink = pylink.JLink()
        except TypeError:
            print("JLink DLL not found, skipping serial number check")
            return
        emulators = jlink.connected_emulators()
        if len(emulators) <= 1:
            return
        if TerminalMenu is None:
            options = [str(e.SerialNumber) for e in emulators]
            sys.exit(
                f"Multiple JTAG emulators connected ({', '.join(options)})\n"
                + "Specify which emulator to use with --snr"
            )

        # Select which emulator should be used
        options = [str(e) for e in emulators]
        terminal_menu = TerminalMenu(options)
        idx = terminal_menu.show()
        if idx is None:
            sys.exit("JLink device not chosen, exiting...")

        serial = str(emulators[idx].SerialNumber)

        launch["configurations"][0]["serialNumber"] = serial
        launch["configurations"][1]["serialNumber"] = serial

    def _openocd_svd_search(self, vscode_folder: pathlib.Path, device: str):
        try:
            from pyocd.target.pack import pack_target
        except ImportError:
            return None

        # Find the first pack that matches the start of the device name
        # This probably won't match the actual SoC part number, but should refer back to the same SVD file.
        for pack in pack_target.ManagedPacks.get_installed_targets():
            if pack.part_number.lower().startswith(device):
                break
        else:
            return None
        svd_folder = vscode_folder / "svd"
        svd_folder.mkdir(exist_ok=True)
        svd_output = svd_folder / f"{device}.svd"
        with svd_output.open("wb") as f:
            f.write(pack.svd.read(-1))

        return str(svd_output)

    def _openocd(self, build_dir, cache, runners_yaml, vscode_folder):
        board_dir = cache["BOARD_DIR"]
        cfg_path = f"{board_dir}/support/openocd.cfg"

        launch["configurations"][0]["servertype"] = "openocd"
        launch["configurations"][1]["servertype"] = "openocd"
        launch["configurations"][0]["serverpath"] = cache.get("OPENOCD")
        launch["configurations"][1]["serverpath"] = cache.get("OPENOCD")

        launch["configurations"][0]["configFiles"] = [cfg_path]
        launch["configurations"][1]["configFiles"] = [cfg_path]
        launch["configurations"][0]["rtos"] = "Zephyr"
        launch["configurations"][1]["rtos"] = "Zephyr"

        if device := self._jlink_device_name(runners_yaml):
            svd_path = self._openocd_svd_search(vscode_folder, device.lower())
            if svd_path is not None:
                launch["configurations"][0]["svdFile"] = svd_path
                launch["configurations"][1]["svdFile"] = svd_path

    def _physical_hardware(self, build_dir, cache):
        if build_dir.parts[-1] == "tfm":
            self._tfm_build(build_dir, cache)
        else:
            self._zephyr_build(build_dir, cache)

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

            # Exclude common build paths to cleanup search results
            if args.search_exclude:
                # Documentation build output folder
                settings["search.exclude"]["**/_build/*"] = True
                # Babblesim build output folder
                settings["search.exclude"]["**/bsim_out/*"] = True
                # Twister run output directories
                settings["search.exclude"]["twister-out*"] = True
                # Default output prefix for `west release-build`
                settings["search.exclude"]["release-*"] = True

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
                    f"{build_dir} does not appear to be a valid cmake build directory"
                )
                sys.exit(1)
            if (build_dir / "_sysbuild").exists():
                log.err(
                    f"{build_dir} is a sysbuild directory, not an application directory"
                )
                sys.exit(1)

            cache = zcmake.CMakeCache.from_build_dir(build_dir)
            runners_yaml = None
            if build_dir.parts[-1] == "tfm":
                runners_yaml_path = build_dir / ".." / "zephyr" / "runners.yaml"
            else:
                runners_yaml_path = build_dir / "zephyr" / "runners.yaml"
            if runners_yaml_path.exists():
                with pathlib.Path(runners_yaml_path).open("r", encoding="utf-8") as f:
                    runners_yaml = yaml.safe_load(f)

            c_cpp_properties["configurations"][0]["compileCommands"] = str(
                build_dir / "compile_commands.json"
            )

            if runners_yaml is None:
                if cache["BOARD"] == "unit_testing":
                    self._native(build_dir, cache)
                else:
                    self._qemu(build_dir, cache)
            elif runners_yaml["debug-runner"] == "native":
                self._native(build_dir, cache)
            elif runners_yaml["debug-runner"] == "jlink":
                self._physical_hardware(build_dir, cache)
                self._jlink(args.snr, build_dir, cache, runners_yaml)
            elif runners_yaml["debug-runner"] == "openocd":
                self._physical_hardware(build_dir, cache)
                self._openocd(build_dir, cache, runners_yaml, vscode_folder)

            log.inf(
                f"Writing `c_cpp_properties.json` and `launch.json` to {vscode_folder}"
            )

            with (vscode_folder / "c_cpp_properties.json").open("w") as f:
                json.dump(c_cpp_properties, f, indent=4)
            with (vscode_folder / "launch.json").open("w") as f:
                json.dump(launch, f, indent=4)
