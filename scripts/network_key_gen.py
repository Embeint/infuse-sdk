#!/usr/bin/env python3

import argparse
import pathlib
import secrets
import sys

import yaml


def hexint_presenter(dumper, data):
    return dumper.represent_int(f"0x{data:06x}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate a network key file", allow_abbrev=False)
    parser.add_argument(
        "--id",
        type=lambda x: int(x, 16),
        required=True,
        help="Unique network identifier",
    )
    parser.add_argument("output", type=str, help="Output filename")
    args = parser.parse_args()

    if args.id < 0 or args.id > 0xFFFFFF:
        sys.exit("ID must be in range [0x000000, 0xFFFFFF]")

    contents = {
        "id": args.id,
        "key": secrets.token_bytes(32),
    }
    yaml.add_representer(int, hexint_presenter)

    with pathlib.Path(args.output).open("w", encoding="utf-8") as f:
        yaml.dump(contents, f)
