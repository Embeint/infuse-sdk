#!/usr/bin/env python3
# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Check that test tags cover every Infuse-IoT area each test depends on.

`scripts/ci/test_plan.py` only runs a test on a pull request if one of its
tags matches a changed file, so a missing dependency tag means a test that
should run is silently skipped.

The first tag of each test is its own area. The remaining tags must
include every area (as defined by `scripts/ci/tags.yaml`) that the test:
  * includes headers from (`#include <infuse/...>`)
  * enables Kconfig symbols from, including symbols those `select`/`imply`,
    and symbols turned on by the Infuse-IoT default configurations
    (`kconfig/Kconfig.defaults.*`, e.g. everything `CONFIG_INFUSE_SDK=y`
    enables). `default y` entries in the test's own Kconfig count as enabled.
  * references files from in its CMakeLists.txt
and every area that the sources of the module under test include headers
from (sources guarded by a symbol of the test's own area, whose CMake
conditions the enabled symbols may satisfy).

Extra tags are allowed, so tags that cannot be derived can be added by hand,
e.g. drivers enabled through devicetree, or areas only reached through the
headers or sources of other modules.

Usage:
    check_test_tags.py [--fix] [TEST_DIR ...]
"""

import argparse
import pathlib
import re
import sys

import yaml
from tag_map import SDK, TagMap, find_yamls, test_tags

SOURCE_ROOTS = ("subsys", "lib", "drivers")
KCONFIG_ROOTS = SOURCE_ROOTS + ("generated", "kconfig")

INCLUDE_RE = re.compile(r"#\s*include\s*<infuse/([^>]+)>")
CONFIG_RE = re.compile(r"\bCONFIG_(\w+)=([^\s\"']+|\"[^\"]*\")")
KCONFIG_SYM_RE = re.compile(r"^\s*(?:menu)?config\s+(\w+)")
KCONFIG_SEL_RE = re.compile(r"^\s*(select|imply)\s+(\w+)")
KCONFIG_DEFAULT_Y_RE = re.compile(r"^\s*default\s+y\s*(?:#.*)?$")
KCONFIG_DEFAULT_Y_IF_RE = re.compile(r"^\s*default\s+y(?:\s+if\s+(.*?))?\s*(?:#.*)?$")
KCONFIG_ENTRY_RE = re.compile(r"^\s*(?:menu)?config(?:default)?\s+(\w+)")
KCONFIG_IF_RE = re.compile(r"^\s*if\s+(.*?)\s*(?:#.*)?$")
KCONFIG_ENDIF_RE = re.compile(r"^\s*endif\b")
KCONFIG_DEPENDS_RE = re.compile(r"^\s*depends\s+on\s+(.*?)\s*(?:#.*)?$")
KCONFIG_RSOURCE_RE = re.compile(r'^\s*rsource\s+"([^"]+)"')
KCONFIG_TOKEN_RE = re.compile(r"\s*(&&|\|\||!|\(|\)|\w+|\S+)")
# Infuse-IoT default configurations, applied by `CONFIG_INFUSE_SDK=y` and similar
KCONFIG_DEFAULTS_ROOT = "Kconfig.infuse"
KCONFIG_DEFAULTS_DIR = "kconfig"
COMMENT_RE = re.compile(r"(?:^|\s)#.*$")
CMAKE_COMMENT_RE = re.compile(r"#.*$", re.M)
CMAKE_CMD_RE = re.compile(r"\b(\w+)\s*\(([^)]*)\)")
CMAKE_SYM_RE = re.compile(r"^(?:\$\{)?CONFIG_(\w+)\}?$")
CMAKE_PATH_RE = re.compile(r"(?:\$\{INFUSE_SDK_BASE\}/|(?:\.\./)+)([\w./-]+)")


class Kconfig:
    """Infuse Kconfig symbols: defining area, select/imply graph and compiled sources."""

    def __init__(self, tagmap):
        self.area = {}
        self.selects = {}
        self.implies = {}
        self.sources = []
        self.default_on = set()
        self.default_rules = []
        self.depends = {}
        self._parse_defaults(SDK / KCONFIG_DEFAULTS_ROOT, ())
        for root in KCONFIG_ROOTS:
            for kc in (SDK / root).rglob("Kconfig*"):
                tags = tagmap.tags(str(kc.relative_to(SDK)))
                cur = None
                stack = []
                for line in kc.read_text(errors="ignore").splitlines():
                    if m := KCONFIG_IF_RE.match(line):
                        stack.append(m.group(1))
                        cur = None
                    elif KCONFIG_ENDIF_RE.match(line):
                        if stack:
                            stack.pop()
                        cur = None
                    elif m := KCONFIG_SYM_RE.match(line):
                        cur = m.group(1)
                        self.area.setdefault(cur, set()).update(tags)
                        self.depends.setdefault(cur, set()).update(stack)
                    elif (m := KCONFIG_DEPENDS_RE.match(line)) and cur:
                        self.depends[cur].add(m.group(1))
                    elif (m := KCONFIG_SEL_RE.match(line)) and cur:
                        graph = self.selects if m.group(1) == "select" else self.implies
                        graph.setdefault(cur, set()).add(m.group(2))
                    elif KCONFIG_DEFAULT_Y_RE.match(line) and cur:
                        self.default_on.add(cur)
        visited = set()
        for root in SOURCE_ROOTS:
            self._walk_cmake(SDK / root, (), visited)
        # Directories not reached through add_subdirectory() from the roots
        for root in SOURCE_ROOTS:
            for cm in sorted((SDK / root).rglob("CMakeLists.txt")):
                if cm.parent not in visited:
                    self._walk_cmake(cm.parent, (), visited)

    @staticmethod
    def evaluate(expr, enabled):
        """Evaluate a Kconfig expression: True, False, or None when unknown.

        Plain symbols are true when enabled. `!`, `&&`, `||` and parentheses are
        supported with three-valued logic; anything else (macros, comparisons,
        strings) is unknown.
        """
        tokens = KCONFIG_TOKEN_RE.findall(expr)
        pos = 0

        def peek():
            return tokens[pos] if pos < len(tokens) else None

        def take():
            nonlocal pos
            pos += 1
            return tokens[pos - 1]

        def unary():
            tok = take() if peek() is not None else None
            if tok == "!":
                val = unary()
                return None if val is None else not val
            if tok == "(":
                val = disjunction()
                return val if peek() is not None and take() == ")" else None
            if tok is not None and re.fullmatch(r"[A-Za-z_]\w*", tok) and tok not in ("y", "n"):
                return tok in enabled
            return None

        def conjunction():
            val = unary()
            while peek() == "&&":
                take()
                rhs = unary()
                val = False if False in (val, rhs) else (None if None in (val, rhs) else True)
            return val

        def disjunction():
            val = conjunction()
            while peek() == "||":
                take()
                rhs = conjunction()
                val = True if True in (val, rhs) else (None if None in (val, rhs) else False)
            return val

        val = disjunction()
        return val if pos == len(tokens) else None

    def depends_met(self, sym, enabled):
        """No dependency of `sym` is known to be unmet."""
        return all(self.evaluate(e, enabled) is not False for e in self.depends.get(sym, ()))

    def _parse_defaults(self, path, conds):
        """Record `default y` rules from the Infuse-IoT default configuration files.

        A `configdefault`/`config` entry with `default y [if EXPR]` is turned on
        when the enclosing `if` conditions and its own condition are known to be
        true and its dependencies are not known to be unmet. Unconditional
        defaults apply to every test equally and are not recorded.
        """
        if not path.is_file():
            return
        stack = []
        cur = None
        for line in path.read_text(errors="ignore").splitlines():
            if m := KCONFIG_IF_RE.match(line):
                stack.append(m.group(1))
                cur = None
            elif KCONFIG_ENDIF_RE.match(line):
                if stack:
                    stack.pop()
                cur = None
            elif m := KCONFIG_RSOURCE_RE.match(line):
                sub = (path.parent / m.group(1)).resolve()
                if sub.is_relative_to(SDK / KCONFIG_DEFAULTS_DIR):
                    self._parse_defaults(sub, conds + tuple(stack))
            elif m := KCONFIG_ENTRY_RE.match(line):
                cur = m.group(1)
            elif cur and (m := KCONFIG_DEFAULT_Y_IF_RE.match(line)):
                required = conds + tuple(stack) + ((m.group(1),) if m.group(1) else ())
                if required:
                    self.default_rules.append((required, cur))

    @staticmethod
    def _condition(args):
        """Symbol groups a CMake `if()` condition requires (any symbol of each group).

        Only `CONFIG_A`, `CONFIG_A OR CONFIG_B ...` and `CONFIG_A AND CONFIG_B ...`
        are understood. Anything else adds no requirement.
        """
        words = args.split()
        syms = [CMAKE_SYM_RE.match(w) for w in words[::2]]
        ops = set(words[1::2])
        if not all(syms) or not ops <= {"OR", "AND"} or len(ops) > 1:
            return ()
        names = frozenset(m.group(1) for m in syms)
        return (names,) if ops == {"OR"} else tuple(frozenset([n]) for n in names)

    def _walk_cmake(self, directory, conds, visited):
        """Record the sources compiled in `directory`, with the conditions guarding them.

        Each source is owned by a guard: the symbol of its `*_sources_ifdef()`, or
        the innermost condition on Infuse-IoT symbols. Sources without one are
        not tracked.

        `conds` is a tuple of symbol groups that must all be satisfied (any symbol
        in a group). Conditions are inherited through `add_subdirectory[_ifdef]()`
        and `if(CONFIG_...)` blocks; `else()` branches add no requirement.
        """
        cm = directory / "CMakeLists.txt"
        if directory in visited or not cm.is_file():
            return
        visited.add(directory)
        stack = []
        text = CMAKE_COMMENT_RE.sub("", cm.read_text(errors="ignore"))
        for cmd, args in CMAKE_CMD_RE.findall(text):
            cmd = cmd.lower()
            words = args.split()
            cur = conds + tuple(g for frame in stack for g in frame)
            if cmd == "if":
                stack.append(self._condition(args))
            elif cmd in ("elseif", "else") and stack:
                stack[-1] = self._condition(args) if cmd == "elseif" else ()
            elif cmd == "endif" and stack:
                stack.pop()
            elif cmd == "add_subdirectory" and words:
                self._walk_cmake(directory / words[0], cur, visited)
            elif cmd == "add_subdirectory_ifdef" and len(words) > 1:
                if m := CMAKE_SYM_RE.match(words[0]):
                    self._walk_cmake(directory / words[1], cur + (frozenset([m.group(1)]),), visited)
            elif cmd.endswith("sources_ifdef") and words:
                if m := CMAKE_SYM_RE.match(words[0]):
                    guard = frozenset([m.group(1)])
                    self._add_sources(directory, words[1:], cur + (guard,), guard)
            elif cmd.endswith("sources"):
                # Owned by the innermost condition on Infuse-IoT symbols
                guard = next((g for g in reversed(cur) if g <= self.area.keys()), None)
                self._add_sources(directory, words, cur, guard)

    def _add_sources(self, directory, files, conds, guard):
        if not guard:
            return
        for f in files:
            if (directory / f).is_file():
                self.sources.append((guard, conds, directory / f))

    def may_be_on(self, sym, enabled, disabled):
        """`sym` is not known to be off.

        Symbols outside Infuse-IoT can be enabled by boards and defaults the test
        files do not show, so only an explicit `=n` rules them out. Infuse-IoT
        symbols must be enabled or have an unconditional `default y`.
        """
        if sym in enabled:
            return True
        if sym in disabled:
            return False
        return sym not in self.area or sym in self.default_on

    def compiled_sources(self, enabled, disabled, primary):
        """Sources whose own guard is an enabled symbol of area `primary`, and whose
        enclosing conditions may be on."""
        for guard, conds, src in self.sources:
            if not any(primary in self.area.get(sym, ()) for sym in guard & enabled):
                continue
            if all(any(self.may_be_on(sym, enabled, disabled) for sym in g) for g in conds):
                yield src

    def closure(self, enabled, disabled=frozenset()):
        """Symbols turned on by `enabled`.

        `select` overrides an explicit `=n`; `imply` and default rules do not.
        """
        enabled = set(enabled)
        todo = list(enabled)
        while True:
            while todo:
                sym = todo.pop()
                implied = self.implies.get(sym, set()) - disabled
                for s in self.selects.get(sym, set()) | implied:
                    if s not in enabled:
                        enabled.add(s)
                        todo.append(s)
            for required, sym in self.default_rules:
                if (
                    sym not in enabled
                    and sym not in disabled
                    and all(self.evaluate(e, enabled) for e in required)
                    and self.depends_met(sym, enabled)
                ):
                    enabled.add(sym)
                    todo.append(sym)
            if not todo:
                return enabled


def header_tags(tagmap, path):
    tags = set()
    for m in INCLUDE_RE.finditer(path.read_text(errors="ignore")):
        tags |= tagmap.header(m.group(1))
    return tags


def required_tags(tagmap, kconfig, test_dir, yaml_path, primary):
    tags = set()
    enabled = set()
    files = [p for p in test_dir.rglob("*") if p.is_file()]
    confs = [p for p in files if p.suffix == ".conf"] + [yaml_path]

    cmake = test_dir / "CMakeLists.txt"
    if cmake.is_file():
        text = cmake.read_text(errors="ignore")
        for m in re.finditer(r"([\w./${}-]+\.conf)", text):
            p = (test_dir / m.group(1)).resolve()
            if p.is_file():
                confs.append(p)
        for m in CMAKE_PATH_RE.finditer(text):
            for base in (SDK, test_dir):
                p = (base / m.group(0).replace("${INFUSE_SDK_BASE}/", "")).resolve()
                if p.exists() and p.is_relative_to(SDK):
                    tags |= tagmap.tags(str(p.relative_to(SDK)))
                    break

    for p in files:
        if p.suffix in (".c", ".h", ".cpp", ".hpp"):
            tags |= header_tags(tagmap, p)

    # Tags are shared by all scenarios, so a symbol enabled by any file or
    # scenario counts as enabled, and only symbols that are never enabled
    # count as disabled.
    disabled = set()
    for p in confs:
        for line in p.read_text(errors="ignore").splitlines():
            for m in CONFIG_RE.finditer(COMMENT_RE.sub("", line)):
                (disabled if m.group(2) == "n" else enabled).add(m.group(1))
    # `default y` in the test's own Kconfig, whatever its condition (e.g. only
    # on some boards), enables the symbol for at least one scenario.
    for p in files:
        if p.name.startswith("Kconfig"):
            cur = None
            for line in p.read_text(errors="ignore").splitlines():
                if m := KCONFIG_ENTRY_RE.match(line):
                    cur = m.group(1)
                elif cur and KCONFIG_DEFAULT_Y_IF_RE.match(line):
                    enabled.add(cur)
    disabled -= enabled

    enabled = kconfig.closure(enabled, disabled)
    for sym in enabled:
        tags |= kconfig.area.get(sym, set())
    for src in kconfig.compiled_sources(enabled, disabled, primary):
        tags |= header_tags(tagmap, src)

    return tags - {primary}


def _indent(line):
    return len(line) - len(line.lstrip(" "))


def _is_content(line):
    s = line.strip()
    return bool(s) and not s.startswith("#")


def write_common_tags(yaml_path, tags):
    """Replace the `common: tags:` entry, preserving the file's indentation."""
    original = yaml_path.read_text()
    lines = original.splitlines(keepends=True)

    # Locate the top-level `common:` block
    start = next((i for i, ln in enumerate(lines) if re.match(r"^common:\s*(#.*)?$", ln)), None)
    if start is None:
        raise ValueError(f"{yaml_path}: no top-level `common:` block")
    end = next(
        (i for i in range(start + 1, len(lines)) if _is_content(lines[i]) and _indent(lines[i]) == 0),
        len(lines),
    )
    children = [i for i in range(start + 1, end) if _is_content(lines[i])]
    if not children:
        raise ValueError(f"{yaml_path}: empty `common:` block")
    key_indent = _indent(lines[children[0]])

    # Locate the `tags:` key at the block's own indentation level
    tag_line = next(
        (i for i in children if _indent(lines[i]) == key_indent and re.match(r"^\s*tags:", lines[i])),
        None,
    )
    if tag_line is None:
        raise ValueError(f"{yaml_path}: no `tags:` entry in `common:`")

    # The value continues on following lines that are list items (which may sit
    # at the key's own indentation), deeper-indented lines, blanks or comments.
    stop = tag_line + 1
    if re.match(r"^\s*tags:\s*(#.*)?$", lines[tag_line]):
        while stop < end:
            ln = lines[stop]
            if _is_content(ln) and not (
                _indent(ln) > key_indent or (_indent(ln) == key_indent and ln.lstrip().startswith("- "))
            ):
                break
            stop += 1
        # Leave trailing blank lines and comments in place
        while stop > tag_line + 1 and not _is_content(lines[stop - 1]):
            stop -= 1

    pad = " " * key_indent
    if len(tags) == 1:
        new = [f"{pad}tags: {tags[0]}\n"]
    else:
        new = [f"{pad}tags:\n"] + [f"{pad}  - {t}\n" for t in tags]
    updated = "".join(lines[:tag_line] + new + lines[stop:])

    # Never write a file that doesn't parse back to the requested tags
    data = yaml.safe_load(updated) or {}
    written = (data.get("common") or {}).get("tags")
    written = [written] if isinstance(written, str) else written
    if written != list(tags):
        raise ValueError(f"{yaml_path}: failed to rewrite `common: tags:`")
    yaml_path.write_text(updated)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False
    )
    parser.add_argument("dirs", nargs="*", type=pathlib.Path, help="Test directories to check")
    parser.add_argument("--fix", action="store_true", help="Add missing tags to the test yaml")
    parser.add_argument("--tags", type=pathlib.Path, default=SDK / "scripts/ci/tags.yaml", help="Tag mapping file")
    args = parser.parse_args()

    tagmap = TagMap(args.tags)
    kconfig = Kconfig(tagmap)
    errors = 0

    for y in find_yamls(args.dirs):
        rel = y.relative_to(SDK)
        common, every = test_tags(y)
        if not common:
            print(f"{rel}: no tags in `common:`, first tag must be the test's own area")
            errors += 1
            continue
        primary = common[0]
        missing = sorted(required_tags(tagmap, kconfig, y.parent, y, primary) - every)
        if not missing:
            continue
        if args.fix:
            write_common_tags(y, [primary] + sorted(set(common[1:]) | set(missing)))
            print(f"{rel}: added {', '.join(missing)}")
        else:
            print(f"{rel}: missing tags {', '.join(missing)}")
            errors += 1

    if errors:
        print(f"\n{errors} test(s) with missing tags, run with --fix to add them")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
