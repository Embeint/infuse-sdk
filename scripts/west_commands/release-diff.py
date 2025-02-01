# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Inc

import argparse
import pathlib
import sys
import yaml

from west.commands import WestCommand

try:
    from infuse_iot.diff import diff
except ImportError:
    diff = None

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
            nargs="*",
            help="Original release folder/s",
        )
        parser.add_argument(
            "--output",
            "-o",
            type=pathlib.Path,
            help="Updated release folder",
        )
        parser.add_argument(
            "--tool-compare",
            action="store_true",
            help="Compare diff efficiency between tools",
        )
        return parser

    def do_generation(self, original_dir, output_dir, tool_compare):
        with (original_dir / "manifest.yaml").open("r", encoding="utf-8") as f:
            self.manifest_original = yaml.safe_load(f)
        with (output_dir / "manifest.yaml").open("r", encoding="utf-8") as f:
            self.manifest_output = yaml.safe_load(f)

        def expect_match(field):
            if (
                self.manifest_original["application"][field]
                != self.manifest_output["application"][field]
            ):
                sys.exit("Sysbuild configuration does not match between releases")

        expect_match("TF-M")
        expect_match("sysbuild")
        expect_match("board")
        ver_orig = self.manifest_original["application"]["version"]
        name_orig = self.manifest_original["application"]["primary"]
        name_out = self.manifest_output["application"]["primary"]
        ver_new = self.manifest_output["application"]["version"]

        if self.manifest_original["application"]["sysbuild"]:
            filename = "zephyr.signed.bin"
        else:
            filename = "tfm_s_zephyr_ns_signed.bin"

        input = original_dir / name_orig / "zephyr" / filename
        output = output_dir / name_out / "zephyr" / filename
        imgtool = original_dir / name_orig / "imgtool.yaml"
        with imgtool.open("r", encoding="utf-8") as f:
            imgtool_info = yaml.safe_load(f)
        input_tlv_len = imgtool_info["tlv_area"]["tlv_hdr"]["tlv_tot"]

        print("")
        print(f"{ver_orig} -> {ver_new}")
        # The trailing TLV's can change on device, so exclude them from the original image knowledge
        with open(input, "rb") as f_input:
            with open(output, "rb") as f_output:
                patch = diff.generate(
                    f_input.read(-1)[:-input_tlv_len],
                    f_output.read(-1),
                    True,
                )

        release_dir = output_dir / "diffs"
        release_dir.mkdir(exist_ok=True)
        patch_file = release_dir / f"{ver_orig}.bin"
        with patch_file.open("wb") as f:
            f.write(patch)

        if tool_compare:
            import os
            import shutil
            import subprocess
            import tempfile

            output_len = os.stat(output).st_size
            print("")
            print("Tool Comparison:")
            with tempfile.TemporaryDirectory() as tmp:
                print(f"\t  CPatch: {len(patch)} {100 * len(patch) / output_len:.2f}%")
                if shutil.which("jdiff") is not None:
                    diff_file = f"{tmp}/jdiff.patch"
                    subprocess.run(
                        ["jdiff", str(input), str(output), diff_file],
                        check=False,
                    )
                    jdiff_size = os.stat(diff_file).st_size
                    print(
                        f"\tJojoDiff: {jdiff_size} {100 * jdiff_size / output_len:.2f}%"
                    )
                if shutil.which("bsdiff4") is not None:
                    diff_file = f"{tmp}/bsdiff4.patch"
                    subprocess.run(
                        ["bsdiff4", str(input), str(output), diff_file],
                        check=True,
                    )
                    bs_size = os.stat(diff_file).st_size
                    print(f"\t bsdiff4: {bs_size} {100 * bs_size / output_len:.2f}%")
                if shutil.which("xdelta") is not None:
                    diff_file = f"{tmp}/xdelta.patch"
                    subprocess.run(
                        ["xdelta", "delta", str(input), str(output), diff_file],
                        check=False,
                    )
                    x_size = os.stat(diff_file).st_size
                    print(f"\t  xdelta: {x_size} {100 * x_size / output_len:.2f}%")

    def do_run(self, args, _unknown_args):
        if diff is None:
            sys.exit("infuse-iot.diff not found")

        for original in args.input:
            self.do_generation(original, args.output, args.tool_compare)
