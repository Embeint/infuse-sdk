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
            dest="original",
            type=pathlib.Path,
            help="Original release folder",
        )
        parser.add_argument(
            dest="output",
            type=pathlib.Path,
            help="Updated release folder",
        )
        return parser

    def do_run(self, args, _unknown_args):
        if diff is None:
            sys.exit("infuse-iot.diff not found")

        with (args.original / "manifest.yaml").open("r", encoding="utf-8") as f:
            self.manifest_original = yaml.safe_load(f)
        with (args.output / "manifest.yaml").open("r", encoding="utf-8") as f:
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

        if self.manifest_original["application"]["sysbuild"]:
            filename = "zephyr.signed.bin"
        else:
            filename = "tfm_s_zephyr_ns_signed.bin"

        input = args.original / name_orig / "zephyr" / filename
        output = args.output / name_out / "zephyr" / filename
        imgtool = args.original / name_orig / "imgtool.yaml"
        with imgtool.open("r", encoding="utf-8") as f:
            imgtool_info = yaml.safe_load(f)
        input_tlv_len = imgtool_info["tlv_area"]["tlv_hdr"]["tlv_tot"]

        # The trailing TLV's can change on device, so exclude them from the original image knowledge
        with open(input, "rb") as f_input:
            with open(output, "rb") as f_output:
                patch = diff.generate(
                    f_input.read(-1)[:-input_tlv_len],
                    f_output.read(-1),
                    True,
                )

        release_dir = args.output / "diffs"
        release_dir.mkdir(exist_ok=True)
        patch_file = release_dir / f"{ver_orig}.bin"
        with patch_file.open("wb") as f:
            f.write(patch)
