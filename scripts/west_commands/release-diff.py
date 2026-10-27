# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import yaml
from west.commands import WestCommand

try:
    from infuse_iot.cpatch import Encoder, cpatch
except ImportError:
    Encoder = None
    cpatch = None

# Encoders supported by the `cpatch` binary
CPATCH_BIN_ENCODERS = ("shortest-path", "shortest-path-delta", "stream-thumb2")
CPATCH_INSTALL = "cargo install --git https://github.com/Embeint/python-tools cpatch"

EXPORT_DESCRIPTION = """\
This command generates a diff file between two application releases.
"""


class release_diff(WestCommand):
    def __init__(self):
        super().__init__(
            "release-diff",
            # Keep this in sync with the string in west-commands.yml.
            "Generate a binary diff between two releases",
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False,
        )
        self.cpatch_bin = shutil.which("cpatch")
        self.install_hint_shown = False

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
        )

        parser.add_argument(
            "--input",
            "-i",
            type=pathlib.Path,
            nargs="+",
            help="Original release folder/s",
        )
        parser.add_argument(
            "--output",
            "-o",
            type=pathlib.Path,
            required=True,
            help="Updated release folder",
        )
        parser.add_argument(
            "--tool-compare",
            action="store_true",
            help="Compare diff efficiency between tools",
        )
        parser.add_argument(
            "--ignore-board",
            action="store_true",
            help="Force diff generation even if boards mismatch (revision introduction)",
        )
        return parser

    @staticmethod
    def kconfig(image_dir: pathlib.Path) -> dict[str, str]:
        """Kconfig options the application was built with"""
        config = image_dir / "zephyr" / ".config"
        if not config.exists():
            sys.exit(f"'{str(config)}' does not exist, can't validate application configuration")
        options = {}
        with config.open("r", encoding="utf-8") as f:
            for line in f:
                name, _, value = line.strip().partition("=")
                if name.startswith("CONFIG_"):
                    options[name] = value
        return options

    @staticmethod
    def bootloader_mode(kconfig: dict[str, str]) -> str | None:
        """MCUboot mode the application was built for (None if not using MCUboot)"""
        prefix = "CONFIG_MCUBOOT_BOOTLOADER_MODE_"
        for name, value in kconfig.items():
            if not name.startswith(prefix) or value != "y":
                continue
            mode = name.removeprefix(prefix)
            # Options such as `HAS_NO_DOWNGRADE` are not part of the mode choice
            if not mode.startswith("HAS_"):
                return mode
        return None

    @staticmethod
    def cpatch_encoder(kconfig: dict[str, str]) -> str:
        """Most efficient encoder that the application applying the patch supports"""
        if kconfig.get("CONFIG_INFUSE_CPATCH_VERSION_3") == "y":
            return "stream-thumb2"
        if kconfig.get("CONFIG_INFUSE_CPATCH_VERSION_2") == "y":
            return "shortest-path-delta"
        return "shortest-path"

    def cpatch_generate(self, bin_original: bytes, bin_new: bytes, verbose: bool, encoder: str) -> bytes | None:
        """Generate a patch with the `cpatch` binary if available, otherwise the Python library

        Returns None if no available implementation supports the encoder.
        """
        if self.cpatch_bin is not None:
            with tempfile.TemporaryDirectory() as tmp:
                original_f = pathlib.Path(tmp) / "original.bin"
                new_f = pathlib.Path(tmp) / "new.bin"
                patch_f = pathlib.Path(tmp) / "patch.bin"
                original_f.write_bytes(bin_original)
                new_f.write_bytes(bin_new)
                res = subprocess.run(
                    [self.cpatch_bin, "generate", "--encoder", encoder, str(original_f), str(new_f), str(patch_f)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if res.returncode == 0:
                    return patch_f.read_bytes()
                # Older binaries may not support the encoder, fallback to the Python library
                if "invalid encoder" not in res.stderr:
                    sys.exit(f"cpatch failed:\n{res.stderr}")
        if cpatch is not None and encoder in [e.value for e in Encoder]:
            return cpatch.generate(bin_original, bin_new, verbose, encoder=Encoder(encoder))
        return None

    def do_generation(
        self,
        original_dir: pathlib.Path,
        output_dir: pathlib.Path,
        tool_compare: bool,
        ignore_board: bool,
    ):
        original_manifest = original_dir / "manifest.yaml"
        output_manifest = output_dir / "manifest.yaml"
        if not original_manifest.exists():
            sys.exit(f"'{str(original_manifest)}' does not exist, is this a release folder?")
        if not output_manifest.exists():
            sys.exit(f"'{str(output_manifest)}' does not exist, is this a release folder?")

        with original_manifest.open("r", encoding="utf-8") as f:
            self.manifest_original = yaml.safe_load(f)
        with output_manifest.open("r", encoding="utf-8") as f:
            self.manifest_output = yaml.safe_load(f)

        def expect_match(field):
            orig = self.manifest_original["application"][field]
            new = self.manifest_output["application"][field]
            if orig != new:
                if field == "board" and ignore_board:
                    return
                sys.exit(f"Field '{field}' does not match ({orig} != {new})")

        expect_match("TF-M")
        expect_match("sysbuild")
        expect_match("board")
        ver_orig = self.manifest_original["application"]["version"]
        name_orig = self.manifest_original["application"]["primary"]
        name_out = self.manifest_output["application"]["primary"]
        ver_new = self.manifest_output["application"]["version"]

        # The application must write updates in the format the bootloader expects. The bootloader
        # is not updated by DFU, so the MCUboot mode can never change between releases.
        kconfig_orig = self.kconfig(original_dir / name_orig)
        kconfig_new = self.kconfig(output_dir / name_out)
        mode_orig = self.bootloader_mode(kconfig_orig)
        mode_new = self.bootloader_mode(kconfig_new)
        if mode_orig != mode_new:
            sys.exit(f"MCUboot bootloader mode does not match ({mode_orig} != {mode_new})")

        if self.manifest_original["application"]["sysbuild"]:
            filename = "zephyr.signed.bin"
            imgtool_key = 1
        else:
            filename = "tfm_s_zephyr_ns_signed.bin"
            imgtool_key = 2

        input_path = original_dir / name_orig / "zephyr" / filename
        output_path = output_dir / name_out / "zephyr" / filename
        imgtool_orig_f = original_dir / name_orig / "imgtool.yaml"
        imgtool_new_f = output_dir / name_out / "imgtool.yaml"

        if not imgtool_orig_f.exists():
            sys.exit(f"'{str(original_manifest)}' does not exist, can't validate files")
        if not imgtool_new_f.exists():
            sys.exit(f"'{str(output_manifest)}' does not exist, can't validate files")

        with imgtool_orig_f.open("r", encoding="utf-8") as f:
            imgtool_orig = yaml.safe_load(f)
        with imgtool_new_f.open("r", encoding="utf-8") as f:
            imgtool_new = yaml.safe_load(f)

        # Ensure both applications are signed with the same key
        #   (IMAGE_TLV_KEYHASH == 0x01)
        #   (IMAGE_TLV_PUBKEY == 0x02)
        key_orig = next(x for x in imgtool_orig["tlv_area"]["tlvs"] if x["type"] == imgtool_key)
        key_new = next(x for x in imgtool_new["tlv_area"]["tlvs"] if x["type"] == imgtool_key)
        if key_orig["data"] != key_new["data"]:
            sys.exit("Applications were not built with the same signing key")

        id_orig = self.manifest_original["application"]["id"]
        id_new = self.manifest_output["application"]["id"]

        input_tlv_len = imgtool_orig["tlv_area"]["tlv_hdr"]["tlv_tot"]

        print("")
        if id_orig != id_new:
            print(f" 0x{id_orig:08x} -> 0x{id_new:08x}")
        print(f"{ver_orig} -> {ver_new}")
        # The patch is applied by the original application, so it determines the supported format
        encoder = self.cpatch_encoder(kconfig_orig)
        if encoder == "stream-thumb2" and self.cpatch_bin is None and not self.install_hint_shown:
            print(f"`cpatch` binary not found, the Python '{encoder}' encoder is significantly slower")
            print(f"Install the binary with: {CPATCH_INSTALL}")
            self.install_hint_shown = True
        # The trailing TLV's can change on device, so exclude them from the original image knowledge
        with open(input_path, "rb") as f_input, open(output_path, "rb") as f_output:
            bin_original = f_input.read(-1)[:-input_tlv_len]
            bin_new = f_output.read(-1)
        patch = self.cpatch_generate(bin_original, bin_new, True, encoder)
        if patch is None:
            sys.exit(
                f"Original application supports CPatch encoder '{encoder}', update cpatch or infuse-iot to generate it"
            )

        ratio = 100 * len(patch) / len(bin_new)
        print(f"Original File: {len(bin_original):6d} bytes")
        print(f"     New File: {len(bin_new):6d} bytes")
        print(f"   Patch File: {len(patch):6d} bytes ({ratio:.2f}%)")

        release_dir = output_dir / "diffs"
        if id_orig != id_new:
            release_dir /= f"0x{id_orig:08x}"
        release_dir.mkdir(exist_ok=True, parents=True)
        patch_file = release_dir / f"{ver_orig}.bin"
        with patch_file.open("wb") as f:
            f.write(patch)

        if tool_compare:
            output_len = os.stat(output_path).st_size
            print("")
            print("Tool Comparison:")
            with tempfile.TemporaryDirectory() as tmp:
                print(f"\t  CPatch: {len(patch):6d} {100 * len(patch) / output_len:5.2f}% ({encoder}, used)")
                encoders = [*CPATCH_BIN_ENCODERS, *(e.value for e in Encoder or [])]
                for other in dict.fromkeys(encoders):
                    if other == encoder:
                        continue
                    other_patch = self.cpatch_generate(bin_original, bin_new, False, other)
                    if other_patch is None:
                        continue
                    other_len = len(other_patch)
                    print(f"\t  CPatch: {other_len:6d} {100 * other_len / output_len:5.2f}% ({other})")
                if shutil.which("jdiff") is not None:
                    diff_file = f"{tmp}/jdiff.patch"
                    subprocess.run(
                        ["jdiff", str(input_path), str(output_path), diff_file],
                        check=False,
                    )
                    jdiff_size = os.stat(diff_file).st_size
                    print(f"\tJojoDiff: {jdiff_size:6d} {100 * jdiff_size / output_len:5.2f}%")
                if shutil.which("bsdiff4") is not None:
                    diff_file = f"{tmp}/bsdiff4.patch"
                    subprocess.run(
                        ["bsdiff4", str(input_path), str(output_path), diff_file],
                        check=True,
                    )
                    bs_size = os.stat(diff_file).st_size
                    print(f"\t bsdiff4: {bs_size:6d} {100 * bs_size / output_len:5.2f}%")
                if shutil.which("xdelta") is not None:
                    diff_file = f"{tmp}/xdelta.patch"
                    subprocess.run(
                        [
                            "xdelta",
                            "delta",
                            str(input_path),
                            str(output_path),
                            diff_file,
                        ],
                        check=False,
                    )
                    x_size = os.stat(diff_file).st_size
                    print(f"\t  xdelta: {x_size:6d} {100 * x_size / output_len:5.2f}%")

    def do_run(self, args, _unknown_args):
        if cpatch is None and self.cpatch_bin is None:
            sys.exit(f"Neither infuse-iot.cpatch or the `cpatch` binary found, install with: {CPATCH_INSTALL}")

        for original in args.input:
            self.do_generation(original, args.output, args.tool_compare, args.ignore_board)
