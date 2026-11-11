#!/usr/bin/env python3
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Select the twister test roots affected by a set of infuse-sdk changes.

Each changed file is resolved as follows:
  1. Matches scripts/ci/twister_ignore.txt  -> ignored
  2. Inside a test/sample/app directory      -> that directory is tested
  3. Matches a path in scripts/ci/tags.yaml  -> every test tagged with that tag
  4. Anything else                           -> full run

The selected roots are printed to stdout as twister arguments, one per line
(`-T <dir>`), relative to the current working directory. Nothing is printed
if no tests need to run. On a full run, the `--full-root` directories are
printed instead.

Example (from the workspace root):
    infuse-sdk/scripts/ci/test_plan.py -c origin/main..HEAD \\
        --full-root infuse-sdk --full-root modules/nrfconnect/sdk-nrf
"""

import argparse
import fnmatch
import os
import pathlib
import subprocess
import sys

from tag_map import SDK, TEST_ROOTS, TEST_YAMLS, TagMap, find_yamls, test_tags


def log(msg):
    print(msg, file=sys.stderr)


def changed_files(args):
    if args.modified_files:
        return [line.strip() for line in args.modified_files.read_text().splitlines() if line.strip()]
    # Without --no-renames, a moved file is only listed under its new path
    out = subprocess.check_output(
        ["git", "-C", str(SDK), "diff", "--name-only", "--no-renames", args.commits], text=True
    )
    return [f for f in out.splitlines() if f]


def load_ignores(path):
    lines = path.read_text().splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith("#")]


def is_ignored(ignores, f):
    """Last matching pattern wins, `!pattern` re-includes a file."""
    ignored = False
    for p in ignores:
        negate = p.startswith("!")
        if fnmatch.fnmatch(f, p[1:] if negate else p):
            ignored = not negate
    return ignored


def test_dir_of(f):
    """Closest directory containing a test yaml, if the file lives in one."""
    if not f.startswith(tuple(r + "/" for r in TEST_ROOTS)):
        return None
    d = (SDK / f).parent
    while d != SDK:
        if any((d / y).is_file() for y in TEST_YAMLS):
            return d
        d = d.parent
    return None


def all_test_dirs():
    """Test directory -> union of tags across common and all scenarios."""
    return {y.parent: test_tags(y)[1] for y in find_yamls()}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False
    )
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("-c", "--commits", help="Commit range in the form a..b")
    src.add_argument("-m", "--modified-files", type=pathlib.Path, help="File listing changed files, one per line")
    parser.add_argument(
        "--full-root",
        action="append",
        default=[],
        help="Test root to use on a full run (repeatable, default: the infuse-sdk root)",
    )
    parser.add_argument("--tags", type=pathlib.Path, default=SDK / "scripts/ci/tags.yaml", help="Tag mapping file")
    parser.add_argument(
        "--ignore",
        type=pathlib.Path,
        default=SDK / "scripts/ci/twister_ignore.txt",
        help="Ignore pattern file",
    )
    args = parser.parse_args()

    files = changed_files(args)
    ignores = load_ignores(args.ignore)
    tagmap = TagMap(args.tags)

    selected_dirs = set()
    selected_tags = set()
    unresolved = []

    for f in files:
        if is_ignored(ignores, f):
            log(f"  ignored   {f}")
            continue
        d = test_dir_of(f)
        if d is not None:
            selected_dirs.add(d)
            log(f"  test dir  {f}")
            continue
        tags = tagmap.tags(f)
        if tags:
            selected_tags |= tags
            log(f"  tags      {f} -> {', '.join(sorted(tags))}")
            continue
        unresolved.append(f)
        log(f"  UNMAPPED  {f}")

    cwd = pathlib.Path.cwd()
    if unresolved:
        log(f"{len(unresolved)} changed file(s) not mapped to tests: full run")
        roots = args.full_root or [os.path.relpath(SDK, cwd)]
        for r in roots:
            print(f"-T {r}")
        return

    test_dirs = all_test_dirs()
    for d, tags in test_dirs.items():
        if tags & selected_tags:
            selected_dirs.add(d)

    log(f"Tags: {', '.join(sorted(selected_tags)) or '(none)'}")
    log(f"Selected {len(selected_dirs)}/{len(test_dirs)} test directories")
    for d in sorted(selected_dirs):
        print(f"-T {os.path.relpath(d, cwd)}")


if __name__ == "__main__":
    main()
