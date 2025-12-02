# Copyright (c) 2018 Open Source Foundries Limited.
#
# SPDX-License-Identifier: Apache-2.0
"""Common definitions for building Zephyr applications with CMake.

This provides some default settings and convenience wrappers for
building Zephyr applications needed by multiple commands.

See build.py for the build command itself.
"""

from __future__ import annotations

import os
import re
from collections import OrderedDict
from typing import final

DEFAULT_CACHE = "CMakeCache.txt"
DEFAULT_CMAKE_GENERATOR = "Ninja"


@final
class CMakeCacheEntry:
    """Represents a CMake cache entry.

    This class understands the type system in a CMakeCache.txt, and
    converts the following cache types to Python types:

    Cache Type    Python type
    ----------    -------------------------------------------
    FILEPATH      str
    PATH          str
    STRING        str OR list of str (if ';' is in the value)
    BOOL          bool
    INTERNAL      str OR list of str (if ';' is in the value)
    STATIC        str OR list of str (if ';' is in the value)
    UNINITIALIZED str OR list of str (if ';' is in the value)
    ----------    -------------------------------------------
    """

    # Regular expression for a cache entry.
    #
    # CMake variable names can include escape characters, allowing a
    # wider set of names than is easy to match with a regular
    # expression. To be permissive here, use a non-greedy match up to
    # the first colon (':'). This breaks if the variable name has a
    # colon inside, but it's good enough.
    CACHE_ENTRY = re.compile(
        r"""(?P<name>.*?)                                                   # name
         :(?P<type>FILEPATH|PATH|STRING|BOOL|INTERNAL|STATIC|UNINITIALIZED) # type
         =(?P<value>.*)                                                     # value
        """,
        re.X,
    )

    @classmethod
    def _to_bool(cls, val: str):
        # Convert a CMake BOOL string into a Python bool.
        #
        #   "True if the constant is 1, ON, YES, TRUE, Y, or a
        #   non-zero number. False if the constant is 0, OFF, NO,
        #   FALSE, N, IGNORE, NOTFOUND, the empty string, or ends in
        #   the suffix -NOTFOUND. Named boolean constants are
        #   case-insensitive. If the argument is not one of these
        #   constants, it is treated as a variable."
        #
        # https://cmake.org/cmake/help/v3.0/command/if.html
        val = val.upper()
        if val in ("ON", "YES", "TRUE", "Y"):
            return True
        elif (
            val in ("OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND", "") or val.endswith("-NOTFOUND") or val == "NEVER"
        ):
            return False
        else:
            try:
                v = int(val)
                return v != 0
            except ValueError as exc:
                raise ValueError(f"invalid bool {val}") from exc

    @classmethod
    def from_line(cls, line: str, line_no: int) -> None | CMakeCacheEntry:
        # Comments can only occur at the beginning of a line.
        # (The value of an entry could contain a comment character).
        if line.startswith("//") or line.startswith("#"):
            return None

        # Whitespace-only lines do not contain cache entries.
        if not line.strip():
            return None

        m = cls.CACHE_ENTRY.match(line)
        if not m:
            return None

        name, type_, value = (m.group(g) for g in ("name", "type", "value"))
        if type_ == "BOOL":
            try:
                value = cls._to_bool(value)
            except ValueError as exc:
                args = exc.args + (f"on line {line_no}: {line}",)
                raise ValueError(args) from exc
        elif type_ in {"STRING", "INTERNAL", "STATIC", "UNINITIALIZED"} and ";" in value:
            # The value is a CMake list (i.e. is a string which
            # contains a ';'), convert to a Python list.
            value = value.split(";")

        return CMakeCacheEntry(name, value)

    def __init__(self, name: str, value: bool | str | list[str]):
        self.name = name
        self.value = value

    def __str__(self):
        fmt = "CMakeCacheEntry(name={}, value={})"
        return fmt.format(self.name, self.value)


@final
class CMakeCache:
    """Parses and represents a CMake cache file."""

    @staticmethod
    def from_build_dir(build_dir: str) -> CMakeCache:
        return CMakeCache(os.path.join(build_dir, DEFAULT_CACHE))

    def __init__(self, cache_file: str):
        self._entries: OrderedDict[str, CMakeCacheEntry]
        self.cache_file = cache_file
        self.load(cache_file)

    def load(self, cache_file: str):
        entries: list[CMakeCacheEntry] = []
        with open(cache_file, encoding="utf-8") as cache:
            for line_no, line in enumerate(cache):
                entry = CMakeCacheEntry.from_line(line, line_no)
                if entry:
                    entries.append(entry)
        self._entries = OrderedDict((e.name, e) for e in entries)

    def get(self, name: str, default: None | bool | str | list[str] = None) -> None | bool | str | list[str]:
        entry = self._entries.get(name)
        if entry is not None:
            return entry.value
        else:
            return default

    def get_list(self, name: str, default: None | bool | str | list[str] = None):
        if default is None:
            default = []
        entry = self._entries.get(name)
        if entry is not None:
            value = entry.value
            if isinstance(value, list):
                return value
            elif isinstance(value, str):
                return [value] if value else []
            else:
                msg = "invalid value {} type {}"
                raise RuntimeError(msg.format(value, type(value)))
        else:
            return default

    def __contains__(self, name: str):
        return name in self._entries

    def __getitem__(self, name: str):
        return self._entries[name].value

    def __setitem__(self, name: str, entry: CMakeCacheEntry):
        self._entries[name] = entry

    def __delitem__(self, name: str):
        del self._entries[name]

    def __iter__(self):
        return iter(self._entries.values())
