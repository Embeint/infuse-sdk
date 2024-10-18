# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Inc

import argparse
import pathlib
import subprocess
import sys
import shutil
import colorama

from typing_extensions import Tuple

from west.commands import WestCommand

from git import Repo
from yaml import safe_load, dump

import zcmake

EXPORT_DESCRIPTION = """\
This command generates an application release.
"""


class infuse_release(WestCommand):
    def __init__(self):
        super().__init__(
            "infuse-release",
            # Keep this in sync with the string in west-commands.yml.
            "Build application release",
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
        parser.add_argument("-b", "--board", type=str, required=True)
        parser.add_argument("-d", "--source-dir", type=pathlib.Path, required=True)
        parser.add_argument(
            "--ignore-git", action="store_true", help="Ignore git check failures"
        )
        parser.add_argument(
            "--skip-git",
            action="store_true",
            help="Do not run remote git operations",
        )

        return parser

    def do_run(self, args, unknown_args):
        self.args = args
        # TF-M builds should not use sysbuild for now
        self.sysbuild = "/ns" not in self.args.board
        # Validate repository state
        repo = self.validate_state()
        # Expected application version
        expected_int, expected_hex = self.expected_version(repo)
        # Perform release build
        self.do_release_build(expected_int)
        # Validate build configuration
        configs = self.validate_build(expected_int)
        # Export build outputs
        self.export_build(configs, expected_hex)

    def count_commits_ahead_behind(self, repo: Repo):
        # Ensure the repository is up-to-date
        repo.remotes.origin.fetch()

        # Get the active branch
        active_branch = repo.active_branch
        if not active_branch:
            raise RuntimeError("No active branch found")

        # Get the commit objects for local and remote branches
        local_commit = repo.commit(active_branch)
        remote_commit = repo.commit(f"origin/{active_branch.name}")

        # Calculate the ahead and behind counts
        ahead = len(
            list(repo.iter_commits(f"{remote_commit.hexsha}..{local_commit.hexsha}"))
        )
        behind = len(
            list(repo.iter_commits(f"{local_commit.hexsha}..{remote_commit.hexsha}"))
        )

        return ahead, behind

    def validate_state(self) -> Repo:
        repo = Repo(self.args.source_dir, search_parent_directories=True)

        if not self.args.skip_git:
            ahead, behind = self.count_commits_ahead_behind(repo)
            if ahead != 0:
                msg = f"Local repository contains {ahead} commits not present on origin"
                if self.args.ignore_git:
                    print(msg)
                else:
                    sys.exit(msg)
            if behind != 0:
                msg = f"Local repository is missing {behind} commits present on origin"
                if self.args.ignore_git:
                    print(msg)
                else:
                    sys.exit(msg)
        return repo

    def expected_version(self, repo: Repo) -> Tuple[str, str]:
        version_file = self.args.source_dir / "VERSION"
        if not version_file.exists():
            sys.exit(f"{version_file} does not exist")

        with version_file.open("r") as f:
            contents = [l.split("=") for l in f.readlines()]
            m = {l[0].strip(): l[1].strip() for l in contents}

        commit_hash = str(repo.head.commit)
        v_int_tweak = f"{m['VERSION_MAJOR']}.{m['VERSION_MINOR']}.{m['PATCHLEVEL']}+{int(commit_hash[:8], 16)}"
        v_hex_tweak = f"{m['VERSION_MAJOR']}.{m['VERSION_MINOR']}.{m['PATCHLEVEL']}+{commit_hash[:8]}"

        return v_int_tweak, v_hex_tweak

    def do_release_build(self, expected_version):
        name = self.args.source_dir.name
        self.build_dir = pathlib.Path(f"build/release/{self.args.board}/{name}")
        if self.sysbuild:
            self.build_app_dir = self.build_dir / self.build_dir.name
        else:
            self.build_app_dir = self.build_dir

        build_cmd = ["west", "build"]
        build_cmd.extend(["--board", self.args.board])
        build_cmd.extend(["--source-dir", str(self.args.source_dir)])
        build_cmd.extend(["--build-dir", str(self.build_dir)])

        if self.sysbuild:
            build_cmd.extend(["--sysbuild"])
            # TODO: automatically generate tweak for sysbuild
            build_cmd.extend(
                ["--", f'-DCONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="{expected_version}"']
            )

        print("Run build: ", " ".join(build_cmd))
        subprocess.run(build_cmd, check=True)

    def validate_build(self, expected_version: str) -> dict:
        cache = zcmake.CMakeCache.from_build_dir(self.build_app_dir)
        autoconf = (
            self.build_app_dir
            / "zephyr"
            / "include"
            / "generated"
            / "zephyr"
            / "autoconf.h"
        )
        with autoconf.open("r") as f:
            configs = {}
            for l in f.readlines():
                s = l.strip().split(" ")
                configs[s[1]] = s[2].strip("'\"")

        tfm_build = "CONFIG_BUILD_WITH_TFM" in configs
        if "CONFIG_BT_CONN" in configs:
            if "CONFIG_MCUMGR" not in configs:
                print(colorama.Fore.YELLOW + "MCUMGR not enabled with Bluetooth")
            if "CONFIG_MCUMGR_TRANSPORT_BT" not in configs:
                print(colorama.Fore.YELLOW + "MCUMGR BT not enabled with Bluetooth")
            if "CONFIG_MCUMGR_GRP_IMG" not in configs:
                print(
                    colorama.Fore.YELLOW
                    + "MCUMGR Image Management not enabled with Bluetooth"
                )

        if tfm_build:
            signed_bin = self.build_app_dir / "zephyr" / "tfm_s_zephyr_ns_signed.bin"
        else:
            signed_bin = self.build_app_dir / "zephyr" / "zephyr.signed.bin"

        # TODO: replace with cache['IMGTOOL'] once PyPi version updated
        imgtool_path = (
            pathlib.Path(cache["ZEPHYR_BASE"])
            / ".."
            / "bootloader"
            / "mcuboot"
            / "scripts"
            / "imgtool.py"
        )

        # Work out what imgtool says about the image
        imgtool_output = self.build_app_dir / "imgtool.yaml"
        imgtool_cmd = [
            str(imgtool_path),
            "dumpinfo",
            "--outfile",
            str(imgtool_output),
            str(signed_bin),
        ]
        subprocess.run(imgtool_cmd, check=True, capture_output=True)
        with imgtool_output.open("r") as f:
            imgtool_yaml = safe_load(f)

        # Check the version we expected against the version imgtool decodes
        if expected_version != imgtool_yaml["header"]["version"]:
            sys.exit(
                f"Unexpected image versions ({expected_version} != {imgtool_yaml['header']['version']})"
            )

        return configs

    def export_build(self, build_configs: dict, version: str):
        board_normalised = self.args.board.replace("/", "_")
        app_name = self.args.source_dir.name

        output_dir = pathlib.Path(f"{app_name}-{board_normalised}-{version}")
        output_dir.mkdir(parents=True, exist_ok=True)

        # Copy relevant files to output directory
        files = [
            "compile_commands.json",
            "imgtool.yaml",
            "zephyr/.config",
            "zephyr/runners.yaml",
            "zephyr/zephyr.bin",
            "zephyr/zephyr.dts",
            "zephyr/zephyr.elf",
            "zephyr/zephyr.hex",
            "zephyr/zephyr.lst",
            "zephyr/zephyr.map",
            "zephyr/tfm_merged.hex",
            "zephyr/tfm_s_zephyr_ns.bin",
            "zephyr/tfm_s_zephyr_ns.hex",
            "zephyr/tfm_s_zephyr_ns_signed.bin",
            "zephyr/tfm_s_zephyr_ns_signed.hex",
            "zephyr/include/generated/zephyr/autoconf.h",
        ]

        for file in files:
            in_loc = self.build_app_dir / file
            out_loc = output_dir / file

            if not in_loc.exists():
                continue
            if not out_loc.parent.exists():
                out_loc.parent.mkdir(parents=True)

            shutil.copy(in_loc, out_loc)

        # Create manifest file
        manifest = {
            "application": {
                "id": int(build_configs["CONFIG_INFUSE_APPLICATION_ID"], 0),
                "version": version,
                "board": build_configs["CONFIG_BOARD_TARGET"],
                "soc": build_configs["CONFIG_SOC"],
            },
            "features": {
                "bluetooth": "CONFIG_BT" in build_configs,
                "bluetooth_controller": "CONFIG_BT_CTLR" in build_configs,
                "wifi": "CONFIG_WIFI" in build_configs,
                "lte": "CONFIG_NRF_MODEM_LIB" in build_configs,
                "mcumgr": "CONFIG_MCUMGR" in build_configs,
                "watchdog": "CONFIG_INFUSE_WATCHDOG" in build_configs,
            },
            "commands": [
                c.removeprefix("CONFIG_INFUSE_RPC_COMMAND_").lower()
                for c in build_configs
                if c.startswith("CONFIG_INFUSE_RPC_COMMAND_")
                and not c.endswith("_REQUIRED_AUTH")
            ],
            "kv_keys": [
                k.removeprefix("CONFIG_KV_STORE_KEY_")
                for k in build_configs
                if k.startswith("CONFIG_KV_STORE_KEY_") and not k.endswith("_RANGE")
            ],
        }
        with (output_dir / "manifest.yml").open("w") as f:
            dump(manifest, f)
