# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

import colorama
import pykwalify.core
import yaml
import zcmake
from git import Repo, exc
from west.commands import WestCommand
from west.manifest import ManifestProject, Project

EXPORT_DESCRIPTION = """\
This command generates an application release.
"""

RELEASE_SCHEMA_PATH = str(pathlib.Path(__file__).parent.parent / "schemas" / "release-schema.yml")
with open(RELEASE_SCHEMA_PATH, encoding="utf-8") as f:
    release_schema = yaml.safe_load(f)


class release_build(WestCommand):
    def __init__(self):
        super().__init__(
            "release-build",
            # Keep this in sync with the string in west-commands.yml.
            "Build an Infuse-IoT application release",
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False,
        )
        self.network_key: None | pathlib.Path = None
        self.network_key_secondary: None | pathlib.Path = None

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
            help="Release configuration file",
        )
        parser.add_argument("--ignore-git", action="store_true", help="Ignore git check failures")
        parser.add_argument(
            "--skip-git",
            action="store_true",
            help="Do not run remote git operations",
        )
        return parser

    def _absolute_path(self, relative_root: pathlib.Path, path: str) -> pathlib.Path:
        p = pathlib.Path(path)
        if p.is_absolute():
            return p
        return (relative_root / p).resolve().absolute()

    def do_run(self, args, _unknown_args):
        self.args = args

        with self.args.release.open("r", encoding="utf-8") as f:
            self.release = yaml.safe_load(f)

        try:
            pykwalify.core.Core(source_data=self.release, schema_data=release_schema).validate()
        except pykwalify.errors.SchemaError as e:
            sys.exit(f"ERROR: Malformed section in file: {self.args.release.as_posix()}\n{e}")

        self.application = self._absolute_path(self.args.release.parent, self.release["application_folder"])
        self.signing_key = self._absolute_path(self.args.release.parent, self.release["signing_key"])
        if not self.signing_key.exists():
            sys.exit(f"ERROR: Signing key {self.signing_key} does not exist")
        if key := self.release.get("network_key", None):
            self.network_key = self._absolute_path(self.args.release.parent, key)
            if not self.network_key.exists():
                sys.exit(f"ERROR: Network key {self.network_key} does not exist")
        else:
            self.network_key = None
        if key := self.release.get("network_key_secondary", None):
            self.network_key_secondary = self._absolute_path(self.args.release.parent, key)
            if not self.network_key_secondary.exists():
                sys.exit(f"ERROR: Network key {self.network_key} does not exist")
        else:
            self.network_key_secondary = None

        if override := self.release.get("version_override", None):
            r = r"^\d+\.\d+\.\d+$"
            if not re.match(r, override):
                sys.exit(f"Invalid version format: {override}")
            self.version = override
        else:
            self.version = None

        self.tfm_build = "/ns" in self.release["board"]
        # TF-M builds should not use sysbuild for now
        self.sysbuild = not self.tfm_build
        # Validate state of all manifest repositories
        self.validate_manifest_repos_state()
        # Expected application version
        repo = Repo(self.application, search_parent_directories=True)
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
        try:
            remote_commit = repo.commit(f"origin/{active_branch.name}")
        except exc.BadName as e:
            if self.args.ignore_git:
                print(str(e))
                return 0, 0
            else:
                sys.exit(str(e))

        # Calculate the ahead and behind counts
        ahead = len(list(repo.iter_commits(f"{remote_commit.hexsha}..{local_commit.hexsha}")))
        behind = len(list(repo.iter_commits(f"{local_commit.hexsha}..{remote_commit.hexsha}")))

        return ahead, behind

    def validate_manifest_repos_state(self) -> None:
        if self.args.skip_git:
            return

        print(f"Validating state of {len(self.manifest.projects)} repositories...")
        error_msg = []

        project: Project
        for project in self.manifest.projects:
            absolute_repo_path = os.path.join(self.manifest.topdir, project.path)

            if not os.path.exists(absolute_repo_path):
                # Repo is part of the manifest, but not cloned (probably due to manifest groups).
                # This is not necessarily a problem, but let the user know.
                # Ignore the babblesim repos, most users won't have them cloned out.
                if "babblesim" not in project.groups:
                    print(f"'{project.path}' does not exist (groups: {project.groups})")
                continue

            repo = Repo(absolute_repo_path)

            if isinstance(project, ManifestProject):
                ahead, behind = self.count_commits_ahead_behind(repo)
                if ahead != 0:
                    error_msg.append(
                        f"Manifest project '{project.path}' contains {ahead} commits not present on origin"
                    )
                if behind != 0:
                    error_msg.append(f"Manifest project '{project.path}' is missing {behind} commits present on origin")
            else:
                # Ensure revision specified in manifest matches currently checked out commit
                manifest_commit = repo.commit(project.revision)
                on_disk_commit = repo.commit()
                if manifest_commit != on_disk_commit:
                    err = f"Repository '{project.path}' commit mismatch "
                    err += f"({manifest_commit.hexsha[:8]} != {on_disk_commit.hexsha[:8]})"
                    error_msg.append(err)
            # Ensure there is no uncommitted content on disk
            if repo.is_dirty(untracked_files=True):
                error_msg.append(f"Repository '{project.path}' has uncommitted changes")

        if len(error_msg) > 0:
            msg = os.linesep.join(error_msg)
            if self.args.ignore_git:
                print(msg)
            else:
                sys.exit(msg)

    def expected_version(self, repo: Repo) -> tuple[str, str]:
        version_file = self.application / "VERSION"
        if not version_file.exists():
            sys.exit(f"{version_file} does not exist")

        with version_file.open("r") as f:
            contents = [line.split("=") for line in f.readlines()]
            m = {line[0].strip(): line[1].strip() for line in contents}

        commit_hash = str(repo.head.commit)
        if self.version is None:
            prefix = f"{m['VERSION_MAJOR']}.{m['VERSION_MINOR']}.{m['PATCHLEVEL']}"
        else:
            prefix = self.version

        v_int_tweak = f"{prefix}+{int(commit_hash[:8], 16)}"
        v_hex_tweak = f"{prefix}+{commit_hash[:8]}"
        return v_int_tweak, v_hex_tweak

    def do_spdx_init(self):
        spdx_init_cmd = ["west", "spdx", "--init", "-d"]

        # Prepare application directory for SPDX
        proc = subprocess.run(spdx_init_cmd + [str(self.build_app_dir)], capture_output=True)
        if proc.returncode != 0:
            print("SPDX initialisation stderr:")
            sys.exit(proc.stderr.decode("utf-8"))

    def do_spdx_generate(self):
        spdx_cmd = ["west", "spdx", "-d"]

        # Generate SPDX output
        proc = subprocess.run(spdx_cmd + [str(self.build_app_dir)], capture_output=True)
        if proc.returncode != 0:
            print("SPDX generation stderr:")
            sys.exit(proc.stderr.decode("utf-8"))

    def do_release_build(self, expected_version):
        name = self.application.name
        self.build_dir = pathlib.Path(f"build/release/{self.release['board']}/{name}")
        if self.sysbuild:
            self.build_app_dir = self.build_dir / self.build_dir.name
        else:
            self.build_app_dir = self.build_dir
        signing_key_config = f'"{str(self.signing_key)}"'

        # Remove the build directory for a pristine build
        # Do this because `west build -p` deletes SPDX files
        shutil.rmtree(self.build_dir, True)

        build_cmd = ["west", "build"]
        build_cmd.extend(["--board", self.release["board"]])
        build_cmd.extend(["--source-dir", str(self.application)])
        build_cmd.extend(["--build-dir", str(self.build_dir)])
        if snippets := self.release.get("snippets"):
            snippets_list = snippets.split(";")
            for snippet in snippets_list:
                build_cmd.extend(["-S", snippet])

        if self.sysbuild:
            build_cmd.extend(["--sysbuild"])
            # TODO: automatically generate tweak for sysbuild
            build_cmd.extend(
                [
                    "--",
                    f'-DCONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="{expected_version}"',
                    f"-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE={signing_key_config}",
                    "-DCONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE=y",
                ]
            )
        else:
            build_cmd.extend(
                [
                    "--",
                    f'-DCONFIG_TFM_IMAGE_VERSION_S="{expected_version}"',
                    # Both keys are considered valid by the TF-M BL2
                    f"-DCONFIG_TFM_KEY_FILE_S={signing_key_config}",
                    f"-DCONFIG_TFM_KEY_FILE_NS={signing_key_config}",
                    # Explicitly disable TFM_DUMMY_PROVISIONING, which overrides the
                    # desired keys in the bootloader
                    "-DCONFIG_TFM_DUMMY_PROVISIONING=n",
                    # Disable the debug port when entering the SECURED state, as required by
                    # the PSA RoT device lifecycle.
                    "-DCONFIG_TFM_LCS_SECURED_DISABLE_DEBUG_PORT=y",
                ]
            )

        if self.network_key is not None:
            build_cmd.extend([f'-DCONFIG_INFUSE_SECURITY_DEFAULT_NETWORK="{self.network_key}"'])
        if self.network_key_secondary is not None:
            build_cmd.extend(
                [
                    "-DCONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE=y",
                    f'-DCONFIG_INFUSE_SECURITY_SECONDARY_NETWORK="{self.network_key_secondary}"',
                ]
            )
        build_cmd.extend([f'-DCONFIG_INFUSE_APPLICATION_NAME="{name}"'])
        if self.release.get("disable_logging", False):
            build_cmd.extend(["-DCONFIG_LOG=n"])
        if extra_configs := self.release.get("extra_configs"):
            for c in extra_configs:
                build_cmd.append(f"-D{c}")
        build_cmd.append("-DCONFIG_BUILD_OUTPUT_META=y")

        self.do_spdx_init()
        print("Run build: ", " ".join(build_cmd))
        proc = subprocess.run(build_cmd, capture_output=True)
        if proc.returncode != 0:
            sys.exit(proc.stdout.decode("utf-8"))
        self.build_log = proc.stdout.decode("utf-8")
        self.build_err = proc.stderr.decode("utf-8")
        self.do_spdx_generate()

    def validate_build(self, expected_version: str) -> dict:
        cache = zcmake.CMakeCache.from_build_dir(self.build_app_dir)
        autoconf = self.build_app_dir / "zephyr" / "include" / "generated" / "zephyr" / "autoconf.h"
        warnings = []
        errors = []

        with autoconf.open("r") as f:
            configs = {}
            for line in f.readlines():
                s = line.strip().split(" ")
                configs[s[1]] = s[2].strip("'\"")

        if "CONFIG_BT_CONN" in configs and "CONFIG_BT_HCI_HOST" in configs:
            if "CONFIG_MCUMGR" not in configs:
                warnings.append("MCUMGR not enabled with Bluetooth")
            if "CONFIG_MCUMGR_TRANSPORT_BT" not in configs:
                warnings.append("MCUMGR BT not enabled with Bluetooth")
            if "CONFIG_MCUMGR_GRP_IMG" not in configs:
                warnings.append("MCUMGR Image Management not enabled with Bluetooth")
        if "CONFIG_INFUSE_SECURITY" in configs:
            network_key = configs["CONFIG_INFUSE_SECURITY_DEFAULT_NETWORK"]
            if network_key.endswith("default_network.yaml"):
                warnings.append("Default Infuse-IoT network key used! Communications are not secure!")
        if (
            "CONFIG_INFUSE_RPC_COMMAND_FILE_WRITE_BASIC" in configs
            or "CONFIG_INFUSE_RPC_COMMAND_COAP_DOWNLOAD" in configs
        ) and "CONFIG_INFUSE_DFU_HELPERS" not in configs:
            errors.append("DFU RPCs enabled but image erase helpers not enabled")

        if (
            "CONFIG_EPACKET_INTERFACE_UDP" in configs
            and "CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG" not in configs
        ):
            warnings.append("UDP interface enabled without downlink watchdog")

        if "CONFIG_DNS_RESOLVER_CACHE" in configs:
            warnings.append("Application cannot handle changing IP addresses")

        if self.tfm_build:
            key_file_0 = configs["CONFIG_TFM_KEY_FILE_S"]
            key_file_1 = configs["CONFIG_TFM_KEY_FILE_NS"]
            tfm_key_path = "modules/tee/tf-m/trusted-firmware-m/bl2/ext/mcuboot"
            if (tfm_key_path in key_file_0) or (tfm_key_path in key_file_1):
                errors.append("Default TF-M signing key used! Application is not secure!")
        else:
            if "CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE" not in configs:
                warnings.append("Debug port is not disabled by application")
            key_file = configs["CONFIG_MCUBOOT_SIGNATURE_KEY_FILE"]
            if "bootloader/mcuboot" in key_file:
                errors.append("Default MCUboot signing key used! Application is not secure!")
        if self.tfm_build:
            signed_bin = self.build_app_dir / "zephyr" / "tfm_s_zephyr_ns_signed.bin"
        else:
            signed_bin = self.build_app_dir / "zephyr" / "zephyr.signed.bin"

        # Output validation warnings
        for warn in warnings:
            print(colorama.Fore.YELLOW + warn + colorama.Fore.RESET)
        for err in errors:
            print(colorama.Fore.RED + err + colorama.Fore.RESET)

        # TODO: replace with cache['IMGTOOL'] once PyPi version updated
        imgtool_path = pathlib.Path(cache["ZEPHYR_BASE"]) / ".." / "bootloader" / "mcuboot" / "scripts" / "imgtool.py"

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
            imgtool_yaml = yaml.safe_load(f)

        # Check the version we expected against the version imgtool decodes
        if expected_version != imgtool_yaml["header"]["version"]:
            sys.exit(f"Unexpected image versions ({expected_version} != {imgtool_yaml['header']['version']})")

        return configs

    def export_file(self, dst, src, file):
        if not dst.exists():
            dst.mkdir(parents=True)
        shutil.copy(src / file, dst / file)

    def export_folder(self, dst: pathlib.Path, src: pathlib.Path):
        # Copy relevant files to output directory
        files = [
            "CMakeCache.txt",
            "imgtool.yaml",
            "spdx",
            "zephyr/.config",
            "zephyr/domains.yaml",
            "zephyr/runners.yaml",
            "zephyr/zephyr.bin",
            "zephyr/zephyr.signed.bin",
            "zephyr/zephyr.dts",
            "zephyr/zephyr.elf",
            "zephyr/zephyr.hex",
            "zephyr/zephyr.map",
            "zephyr/tfm_merged.hex",
            "zephyr/tfm_s_zephyr_ns.bin",
            "zephyr/tfm_s_zephyr_ns.hex",
            "zephyr/tfm_s_zephyr_ns_signed.bin",
            "zephyr/tfm_s_zephyr_ns_signed.hex",
            "zephyr/include/generated/zephyr/autoconf.h",
            "zephyr/include/generated/zephyr/devicetree_generated.h",
        ]

        for file in files:
            in_loc = src / file
            out_loc = dst / file

            if not in_loc.exists():
                continue
            if not out_loc.parent.exists():
                out_loc.parent.mkdir(parents=True)

            if in_loc.is_file():
                shutil.copy(in_loc, out_loc)
            else:
                shutil.copytree(in_loc, out_loc, dirs_exist_ok=True)

    def export_build(self, build_configs: dict, version: str):
        board_normalised = self.release["board"].replace("/", "_")
        app_name = self.application.name

        if prefix := self.release.get("output_prefix", False):
            output_dir = pathlib.Path(f"{prefix}-{version}")
        else:
            output_dir = pathlib.Path(f"{app_name}-{board_normalised}-{version}")
        output_dir.mkdir(parents=True, exist_ok=True)
        merged_hex = f"{output_dir.name}.hex"
        ota_bin = f"ota-{output_dir.name}.bin"

        with (output_dir / "build_log.txt").open("w") as f:
            f.write(self.build_log)
        with (output_dir / "build_err.txt").open("w") as f:
            f.write(self.build_err)

        if self.sysbuild:
            domains_file = self.build_dir / "domains.yaml"
            with domains_file.open("r", encoding="utf-8") as f:
                domains_info = yaml.safe_load(f)

            # Export each domain, and build list of hex files to merge
            merge_files = []
            for domain in domains_info["domains"]:
                d = pathlib.Path(domain["build_dir"])
                signed = d / "zephyr" / "zephyr.signed.hex"
                unsigned = d / "zephyr" / "zephyr.hex"
                if signed.exists():
                    merge_files.append(signed)
                else:
                    merge_files.append(unsigned)

                self.export_folder(output_dir / domain["name"], self.build_dir / domain["name"])

            self.export_file(output_dir, self.build_dir, "build_info.yml")
            self.export_file(output_dir, self.build_dir, "domains.yaml")
            self.export_file(output_dir, self.build_dir, "CMakeCache.txt")
            self.export_file(output_dir / "_sysbuild", self.build_dir / "_sysbuild", "autoconf.h")

            # Copy OTA upgrade file to output directory
            shutil.copy(
                output_dir / app_name / "zephyr" / "zephyr.signed.bin",
                output_dir / ota_bin,
            )
            # Merge hex files into the output directory
            cache = zcmake.CMakeCache.from_build_dir(self.build_dir)
            zephyr_base = cache["ZEPHYR_BASE"]
            merge_cmd = [
                "python3",
                zephyr_base + "/scripts/build/mergehex.py",
                "-o",
                str(output_dir / merged_hex),
            ]
            merge_cmd += [str(f) for f in merge_files]
            subprocess.run(merge_cmd, check=True)
            primary_dir = app_name
        else:
            self.export_folder(output_dir / app_name, self.build_dir)
            # Copy TF-M OTA upgrade file to root
            shutil.copy(
                output_dir / app_name / "zephyr" / "tfm_s_zephyr_ns_signed.bin",
                output_dir / ota_bin,
            )
            # Copy TF-M complete file to root
            shutil.copy(
                output_dir / app_name / "zephyr" / "tfm_merged.hex",
                output_dir / merged_hex,
            )
            primary_dir = app_name

        repo = Repo(self.args.release, search_parent_directories=True)
        remotes = [r.url for r in repo.remotes]

        # Create manifest file
        manifest = {
            "configuration": {
                "file": str(self.args.release.resolve().absolute()),
                "repo": {
                    "remotes": remotes,
                    "commit": repo.commit().binsha.hex(),
                },
            },
            "application": {
                "id": int(build_configs["CONFIG_INFUSE_APPLICATION_ID"], 0),
                "version": version,
                "board": build_configs["CONFIG_BOARD_TARGET"],
                "soc": build_configs["CONFIG_SOC"],
                "sysbuild": self.sysbuild,
                "TF-M": self.tfm_build,
                "hex": merged_hex,
                "primary": primary_dir,
            },
            "features": {
                "bluetooth": "CONFIG_BT" in build_configs,
                "bluetooth_controller": "CONFIG_BT_CTLR" in build_configs,
                "wifi": "CONFIG_WIFI" in build_configs,
                "lte": "CONFIG_NRF_MODEM_LIB" in build_configs,
                "mcumgr": "CONFIG_MCUMGR" in build_configs,
                "memfault": "CONFIG_MEMFAULT" in build_configs,
                "watchdog": "CONFIG_INFUSE_WATCHDOG" in build_configs,
            },
            "commands": [
                c.removeprefix("CONFIG_INFUSE_RPC_COMMAND_").lower()
                for c in build_configs
                if c.startswith("CONFIG_INFUSE_RPC_COMMAND_") and not c.endswith("_REQUIRED_AUTH")
            ],
            "kv_keys": [
                k.removeprefix("CONFIG_KV_STORE_KEY_")
                for k in build_configs
                if k.startswith("CONFIG_KV_STORE_KEY_") and not k.endswith("_RANGE")
            ],
        }

        if self.network_key is not None:
            with self.network_key.open("r", encoding="utf-8") as f:
                network = yaml.safe_load(f)
                manifest["application"]["network_id"] = network["id"]

        with (output_dir / "manifest.yaml").open("w") as f:
            yaml.dump(manifest, f)
