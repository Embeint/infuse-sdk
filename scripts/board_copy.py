#!/usr/bin/env python3

import argparse
import shutil
import os
from pathlib import Path

from infuse_iot.util.argparse import ValidDir


def board_copy(args):
    input_base = args.input.name
    output_base = args.output.name

    # Copy the base files
    shutil.copytree(args.input, args.output, dirs_exist_ok=True)

    for filename in os.listdir(args.output):
        if input_base in filename:
            # Replace the old board name in the file path
            new_filename = filename.replace(input_base, output_base)
            os.rename(
                os.path.join(args.output, filename),
                os.path.join(args.output, new_filename),
            )
        else:
            new_filename = filename

        replace_patterns = [
            (input_base, output_base),
            (input_base.upper(), output_base.upper()),
        ]

        file = args.output / new_filename
        # Replace file contents
        with file.open("r", encoding="utf-8") as f:
            contents = f.read()
        for pattern_in, pattern_out in replace_patterns:
            contents = contents.replace(pattern_in, pattern_out)
        with file.open("w", encoding="utf-8") as f:
            f.write(contents)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Copy and rename a board definition", allow_abbrev=False
    )
    parser.add_argument(
        "--input",
        "-i",
        type=ValidDir,
        required=True,
        help="Input board folder",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        required=True,
        help="Output board folder",
    )
    args = parser.parse_args()

    board_copy(args)
