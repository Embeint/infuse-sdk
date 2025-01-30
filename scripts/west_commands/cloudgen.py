# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Inc

import argparse
import pathlib
import json
import os
import importlib
import subprocess

from jinja2 import Environment, FileSystemLoader, select_autoescape

from west.commands import WestCommand

EXPORT_DESCRIPTION = """\
This command generates code from common definitions in Embeint cloud.
"""


class cloudgen(WestCommand):
    def __init__(self):
        super().__init__(
            "cloudgen",
            # Keep this in sync with the string in west-commands.yml.
            "generate files from Infuse-IoT cloud definitions",
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
            "--root",
            type=pathlib.Path,
            default=None,
            help="Root directory for cloud generation (default: script root)",
        )
        parser.add_argument(
            "--project",
            type=str,
            default=None,
            help="Project name for cloud generation (default: infuse)",
        )
        return parser

    def do_run(self, args, unknown_args):
        if args.root is not None and args.project is None:
            print("Error: --project must be specified if --root is provided")
            exit(1)
        self.is_infuse = args.root is None
        self.root_dir = (
            args.root.resolve()
            if args.root
            else pathlib.Path(__file__).parent.parent.parent
        )

        self.template_dir = (
            args.root.resolve() / "scripts" / "templates"
            if args.root
            else pathlib.Path(__file__).parent / "templates"
        )
        self.root_name = self.root_dir.name.replace("-", "_").upper()
        self.project = args.project if args.project else "infuse"

        self.env = Environment(
            loader=FileSystemLoader(pathlib.Path(__file__).parent / "templates"),
            autoescape=select_autoescape(),
            trim_blocks=True,
            lstrip_blocks=True,
        )
        self.tdfgen()
        self.kvgen()
        self.rpcgen()

    def clang_format(self, file):
        args = [
            "clang-format",
            "-i",
            f"--style=file:{pathlib.Path(__file__).parent.parent.parent / '.clang-format'}",
            str(file),
        ]
        subprocess.run(args)

    def _py_type(self, field, struct_prefix: bool):
        ctype_mapping = {
            "uint8_t": "ctypes.c_uint8",
            "uint16_t": "ctypes.c_uint16",
            "uint32_t": "ctypes.c_uint32",
            "uint64_t": "ctypes.c_uint64",
            "int8_t": "ctypes.c_int8",
            "int16_t": "ctypes.c_int16",
            "int32_t": "ctypes.c_int32",
            "int64_t": "ctypes.c_int64",
            "char": "ctypes.c_char",
            "float": "ctypes.c_float",
        }

        t: str = field["type"]
        if t.startswith("struct"):
            base = f"structs.{t[7:]}" if struct_prefix else t[7:]
        else:
            base = ctype_mapping[field["type"]]
        if "num" in field:
            return f"{field['num']} * {base}"
        else:
            return base

    def _array_postfix(self, d, field):
        field["array"] = ""
        if "num" in field:
            if field["num"] == 0:
                field["array"] = "[]"
                d["flexible"] = True
            else:
                field["array"] = f"[{field['num']}]"

    def tdfgen(self):
        tdf_def_file = self.template_dir / "tdf.json"
        tdf_template = self.env.get_template("tdf_definitions.h.jinja")
        tdf_output = self.root_dir / "include" / self.project / "tdf" / "definitions.h"

        if not tdf_def_file.exists():
            return  # Exit early if file doesn't exist

        loader = importlib.util.find_spec("infuse_iot.generated.tdf_definitions")
        tdf_definitions_template = self.env.get_template("tdf_definitions.py.jinja")
        tdf_definitions_output = pathlib.Path(loader.origin)

        with tdf_def_file.open("r") as f:
            tdf_defs = json.load(f)

        for d in tdf_defs["structs"].values():
            for field in d["fields"]:
                self._array_postfix(d, field)
        for d in tdf_defs["definitions"].values():
            for field in d["fields"]:
                self._array_postfix(d, field)

        # Ensure the parent directories and file exist
        tdf_output.parent.mkdir(parents=True, exist_ok=True)
        tdf_output.touch(exist_ok=True)
        with tdf_output.open("w") as f:
            f.write(
                tdf_template.render(
                    is_infuse=self.is_infuse,
                    root=self.root_name,
                    project=self.project.replace("-", "_").upper(),
                    structs=tdf_defs["structs"],
                    definitions=tdf_defs["definitions"],
                )
            )
            f.write(os.linesep)

        def conv_formula(f):
            conv = f"self._{f['name']}"
            i = f["conversion"].get("int", None)
            m = f["conversion"].get("m", 1)
            c = f["conversion"].get("c", 0)

            if i is not None:
                conv = f"int.from_bytes({conv}, byteorder='{i}')"
            if m != 1:
                conv += f" * {m}"
            if c != 0:
                conv += f" + {c}"
            return {"name": f["name"], "conv": conv}

        def display_format(f):
            d = f.get("display")
            fmt = '"{}"'
            if d is None:
                return {"name": f["name"], "fmt": fmt, "postfix": '""'}
            digits = d.get("digits")
            if d.get("fmt") == "hex":
                if digits:
                    fmt = '"0x{{:0{}x}}"'.format(digits)
                else:
                    fmt = '"0x{:x}"'
            if d.get("fmt") == "float":
                if digits:
                    fmt = '"{{:.{}f}}"'.format(digits)
            p = d.get("postfix", "")
            return {"name": f["name"], "fmt": fmt, "postfix": f'"{p}"'}

        for x in ["structs", "definitions"]:
            for s in tdf_defs[x].values():
                s["conversions"] = []
                s["displays"] = []
                for f in s["fields"]:
                    if "conversion" in f:
                        f["py_name"] = f"_{f['name']}"
                        s["conversions"].append(conv_formula(f))
                    else:
                        f["py_name"] = f["name"]
                    s["displays"].append(display_format(f))
                    f["py_type"] = self._py_type(f, True)

        with tdf_definitions_output.open("w", encoding="utf-8") as f:
            f.write(
                tdf_definitions_template.render(
                    structs=tdf_defs["structs"],
                    definitions=tdf_defs["definitions"],
                )
            )
            f.write(os.linesep)

        self.clang_format(tdf_output)

    def kvgen(self):
        kv_def_file = self.template_dir / "kv_store.json"
        kv_defs_template = self.env.get_template("kv_types.h.jinja")
        kv_defs_output = self.root_dir / "include" / self.project / "fs" / "kv_types.h"

        if not kv_def_file.exists():
            return  # Exit early if file doesn't exist

        kv_kconfig_template = self.env.get_template("Kconfig.keys.jinja")
        kv_kconfig_output = (
            self.root_dir / "subsys" / "fs" / "kv_store" / "Kconfig.keys"
        )

        kv_keys_template = self.env.get_template("kv_keys.c.jinja")
        kv_keys_output = self.root_dir / "subsys" / "fs" / "kv_store" / "kv_keys.c"

        loader = importlib.util.find_spec("infuse_iot.generated.kv_definitions")
        kv_py_template = self.env.get_template("kv_definitions.py.jinja")
        kv_py_output = pathlib.Path(loader.origin)

        with kv_def_file.open("r") as f:
            kv_defs = json.load(f)
        kv_defs["definitions"] = {int(k): v for k, v in kv_defs["definitions"].items()}

        # Ensure the parent directories and file exist
        kv_kconfig_output.parent.mkdir(parents=True, exist_ok=True)
        kv_kconfig_output.touch(exist_ok=True)
        with kv_kconfig_output.open("w") as f:
            f.write(kv_kconfig_template.render(definitions=kv_defs["definitions"]))

        with kv_keys_output.open("w") as f:
            for d in kv_defs["definitions"].values():
                flags = []
                if d.get("reflect", False):
                    flags.append("KV_FLAGS_REFLECT")
                if d.get("write_only", False):
                    flags.append("KV_FLAGS_WRITE_ONLY")
                if d.get("read_only", False):
                    flags.append("KV_FLAGS_READ_ONLY")
                if len(flags) > 0:
                    d["flags"] = " | ".join(flags)
                else:
                    d["flags"] = 0
                if "range" in d:
                    d["range_val"] = f"CONFIG_KV_STORE_KEY_{d['name']}_RANGE"
                else:
                    d["range_val"] = 1
            f.write(kv_keys_template.render(definitions=kv_defs["definitions"]))
            f.write(os.linesep)

        # Ensure the parent directories and file exist
        kv_defs_output.parent.mkdir(parents=True, exist_ok=True)
        kv_defs_output.touch(exist_ok=True)
        with kv_defs_output.open("w") as f:
            # Simplify template logic for array postfix
            def array_postfix(d, field):
                field["array"] = ""
                if "num" in field:
                    if field["num"] == 0:
                        field["array"] = "[]"
                        field["flexible"] = True
                        d["flexible"] = True
                    else:
                        field["array"] = f"[{field['num']}]"

            for d in kv_defs["structs"].values():
                for field in d["fields"]:
                    array_postfix(d, field)
            for d in kv_defs["definitions"].values():
                for field in d["fields"]:
                    array_postfix(d, field)
                    # If contained struct is flexible, so is this struct
                    if field["type"].startswith("struct "):
                        s = field["type"].removeprefix("struct ")
                        if kv_defs["structs"][s].get("flexible", False):
                            field["flexible_type"] = s
                            d["flexible"] = True

            f.write(
                kv_defs_template.render(
                    is_infuse=self.is_infuse,
                    root=self.root_name,
                    project=self.project.replace("-", "_").upper(),
                    structs=kv_defs["structs"],
                    definitions=kv_defs["definitions"],
                )
            )
            f.write(os.linesep)

        for x in ["structs", "definitions"]:
            for s in kv_defs[x].values():
                for f in s["fields"]:
                    f["py_type"] = self._py_type(f, True)

        with kv_py_output.open("w", encoding="utf-8") as f:
            f.write(
                kv_py_template.render(
                    structs=kv_defs["structs"],
                    definitions=kv_defs["definitions"],
                )
            )
            f.write(os.linesep)

        self.clang_format(kv_defs_output)
        self.clang_format(kv_keys_output)

    def rpcgen(self):
        rpc_def_file = self.template_dir / "rpc.json"
        rpc_defs_template = self.env.get_template("rpc_types.h.jinja")
        rpc_defs_output = self.root_dir / "include" / self.project / "rpc" / "types.h"

        if not rpc_def_file.exists():
            return  # Exit early if file doesn't exist

        rpc_kconfig_template = self.env.get_template("Kconfig.commands.jinja")
        rpc_kconfig_output = (
            self.root_dir / "subsys" / "rpc" / "commands" / "Kconfig.commands"
        )

        rpc_commands_template = self.env.get_template("rpc_commands.h.jinja")
        rpc_commands_output = (
            self.root_dir / "subsys" / "rpc" / "commands" / "commands.h"
        )

        rpc_runner_template = self.env.get_template("rpc_runner.c.jinja")
        rpc_runner_output = self.root_dir / "subsys" / "rpc" / "command_runner.c"

        loader = importlib.util.find_spec("infuse_iot.generated.rpc_definitions")
        rpc_defs_py_template = self.env.get_template("rpc_definitions.py.jinja")
        rpc_defs_py_output = pathlib.Path(loader.origin)

        with rpc_def_file.open("r") as f:
            rpc_defs = json.load(f)

        # Ensure the parent directories and file exist
        rpc_kconfig_output.parent.mkdir(parents=True, exist_ok=True)
        rpc_kconfig_output.touch(exist_ok=True)
        with rpc_kconfig_output.open("w") as f:
            f.write(rpc_kconfig_template.render(commands=rpc_defs["commands"]))

        with rpc_commands_output.open("w") as f:
            f.write(
                rpc_commands_template.render(
                    root=self.root_name,
                    commands=rpc_defs["commands"],
                )
            )
            f.write(os.linesep)

        with rpc_runner_output.open("w") as f:
            f.write(rpc_runner_template.render(commands=rpc_defs["commands"]))
            f.write(os.linesep)

        # Ensure the parent directories and file exist
        rpc_defs_output.parent.mkdir(parents=True, exist_ok=True)
        rpc_defs_output.touch(exist_ok=True)
        with rpc_defs_output.open("w") as f:
            for d in rpc_defs["structs"].values():
                for field in d["fields"]:
                    self._array_postfix(d, field)
            for d in rpc_defs["commands"].values():
                for field in d["request_params"]:
                    self._array_postfix(d, field)
                for field in d["response_params"]:
                    self._array_postfix(d, field)

            def enum_type_replace(field):
                if field["type"].startswith("enum"):
                    n = field["type"].removeprefix("enum ")
                    field["type"] = rpc_defs["enums"][n]["type"]

            # Swap enum types back to underlying type
            for s in rpc_defs["structs"].values():
                for field in s["fields"]:
                    enum_type_replace(field)
            for c in rpc_defs["commands"].values():
                for field in c["request_params"]:
                    enum_type_replace(field)
                for field in c["response_params"]:
                    enum_type_replace(field)

            f.write(
                rpc_defs_template.render(
                    is_infuse=self.is_infuse,
                    root=self.root_name,
                    project=self.project.replace("-", "_").upper(),
                    structs=rpc_defs["structs"],
                    enums=rpc_defs["enums"],
                    commands=rpc_defs["commands"],
                )
            )
            f.write(os.linesep)

        with rpc_defs_py_output.open("w") as f:
            for s in rpc_defs["structs"].values():
                for field in s["fields"]:
                    field["py_name"] = field["name"]
                    field["py_type"] = self._py_type(field, False)
            for e in rpc_defs["enums"].values():
                for value in e["values"]:
                    value["py_name"] = value["name"]
            for c in rpc_defs["commands"].values():
                for sub in ["request_params", "response_params"]:
                    for field in c[sub]:
                        field["py_type"] = self._py_type(field, False)

            f.write(
                rpc_defs_py_template.render(
                    structs=rpc_defs["structs"],
                    enums=rpc_defs["enums"],
                    commands=rpc_defs["commands"],
                )
            )

        self.clang_format(rpc_defs_output)
        self.clang_format(rpc_commands_output)
        self.clang_format(rpc_runner_output)
