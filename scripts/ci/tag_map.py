# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Test tag helpers shared by test_plan.py and check_test_tags.py.

Both scripts must agree on how files map to tags and how tests are tagged,
otherwise the tag checker validates against different rules than the ones
used to select tests.
"""

import pathlib

import yaml

SDK = pathlib.Path(__file__).resolve().parents[2]
TEST_ROOTS = ("apps", "samples", "tests")
TEST_YAMLS = ("testcase.yaml", "sample.yaml")
INCLUDE_ROOTS = ("include", "generated/include", "generated/include_always")


class TagMap:
    """Path -> tags, from scripts/ci/tags.yaml.

    A `files` entry ending in `/` matches everything below that directory,
    any other entry matches that exact file.
    """

    def __init__(self, path):
        cfg = yaml.safe_load(path.read_text()) or {}
        self.entries = [(f, tag) for tag, e in cfg.items() for f in e.get("files", [])]

    def tags(self, rel):
        return {tag for f, tag in self.entries if (f.endswith("/") and rel.startswith(f)) or rel == f}

    def header(self, name):
        tags = set()
        for root in INCLUDE_ROOTS:
            tags |= self.tags(f"{root}/infuse/{name}")
        return tags


def _as_list(tags):
    return [tags] if isinstance(tags, str) else list(tags or [])


def test_tags(yaml_path):
    """Tags of a test yaml: (`common:` tags in order, union across all scenarios)."""
    data = yaml.safe_load(yaml_path.read_text()) or {}
    common = _as_list((data.get("common") or {}).get("tags"))
    every = set(common)
    for s in (data.get("tests") or {}).values():
        every.update(_as_list((s or {}).get("tags")))
    return common, every


def find_yamls(roots=None):
    """Test yamls below `roots` (default: every test root of the SDK)."""
    roots = [pathlib.Path(r).resolve() for r in roots] if roots else [SDK / r for r in TEST_ROOTS]
    found = set()
    for root in roots:
        for name in TEST_YAMLS:
            found.update(root.rglob(name))
    return sorted(found)
