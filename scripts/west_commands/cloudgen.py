# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import pathlib
import json
import importlib
import subprocess
import sys

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
            "--output",
            "-o",
            type=str,
            required=True,
            help="Output module for the generated files",
        )
        parser.add_argument(
            "--defs", "-d", type=str, help="Folder containing extentsion definitions"
        )
        return parser

    def do_run(self, args, unknown_args):
        self.extra_defs_base = pathlib.Path(args.defs) if args.defs else None
        self.output_base = pathlib.Path(args.output)
        self.generate_base = self.output_base / "generated"
        self.definition_dir = pathlib.Path(__file__).parent / "cloud_definitions"
        self.template_dir = pathlib.Path(__file__).parent / "templates"
        self.infuse_root_dir = pathlib.Path(__file__).parent.parent.parent
        self.env = Environment(
            loader=FileSystemLoader(self.template_dir),
            autoescape=select_autoescape(),
            keep_trailing_newline=True,
            trim_blocks=True,
            lstrip_blocks=True,
        )

        if self.extra_defs_base and not self.extra_defs_base.exists():
            sys.exit(f"Path '{self.extra_defs_base}' does not exist")

        self.tdfgen()
        self.kvgen()
        self.rpcgen()

        print(f"Outputs written to '{self.generate_base.absolute()}'")

    def clang_format(self, file):
        args = [
            "clang-format",
            "-i",
            f"--style=file:{self.infuse_root_dir / '.clang-format'}",
            str(file),
        ]
        subprocess.run(args)

    def ruff_format(self, file):
        args = [
            "ruff",
            "format",
            "-q",
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
        tdf_def_file = self.definition_dir / "tdf.json"
        tdf_template = self.env.get_template("tdf_definitions.h.jinja")
        tdf_output = self.generate_base / "include" / "infuse" / "tdf" / "definitions.h"
        tdf_output.parent.mkdir(parents=True, exist_ok=True)

        loader = importlib.util.find_spec("infuse_iot.generated.tdf_definitions")
        tdf_definitions_template = self.env.get_template("tdf_definitions.py.jinja")
        tdf_definitions_output = pathlib.Path(loader.origin)
        tdf_extensions_exist = False

        with tdf_def_file.open("r") as f:
            tdf_defs = json.load(f)
        if self.extra_defs_base:
            tdf_def_file_ext = self.extra_defs_base / "tdf.json"
            if tdf_def_file_ext.exists():
                tdf_extensions_exist = True
                with tdf_def_file_ext.open("r") as f:
                    tdf_defs_ext = json.load(f)
                    # Ensure IDs sit in extension range
                    for tdf_id, tdf_def in tdf_defs_ext["definitions"].items():
                        assert int(tdf_id) > 1024
                        tdf_def["extension"] = True
                    # Ensure no struct name collisions
                    for struct_name, struct_def in tdf_defs_ext["structs"].items():
                        assert struct_name not in tdf_defs["structs"]
                        struct_def["extension"] = True
                # Merge extensions into base definitions
                tdf_defs["structs"].update(tdf_defs_ext["structs"])
                tdf_defs["definitions"].update(tdf_defs_ext["definitions"])

        for d in tdf_defs["structs"].values():
            for field in d["fields"]:
                self._array_postfix(d, field)
        for d in tdf_defs["definitions"].values():
            for field in d["fields"]:
                self._array_postfix(d, field)
            d["only_flexible"] = (
                len(d["fields"]) == 1 and d["fields"][0]["array"] == "[]"
            )

        with tdf_output.open("w") as f:
            f.write(
                tdf_template.render(
                    structs=tdf_defs["structs"], definitions=tdf_defs["definitions"]
                )
            )

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

        def generate(output: pathlib.Path, extensions: bool):
            with output.open("w", encoding="utf-8") as f:
                f.write(
                    tdf_definitions_template.render(
                        structs=tdf_defs["structs"],
                        definitions=tdf_defs["definitions"],
                        extensions=extensions,
                    )
                )

        generate(tdf_definitions_output, False)
        if tdf_extensions_exist:
            py_extensions = self.generate_base / "tdf_definitions.py"
            generate(py_extensions, True)
            self.ruff_format(py_extensions)

        self.clang_format(tdf_output)
        self.ruff_format(tdf_definitions_output)

    def kvgen(self):
        kv_def_file = self.definition_dir / "kv_store.json"
        kv_defs_template = self.env.get_template("kv_types.h.jinja")
        kv_defs_output = self.generate_base / "include" / "infuse" / "fs" / "kv_types.h"
        kv_defs_output.parent.mkdir(parents=True, exist_ok=True)
        kv_extensions_exist = False

        kv_kconfig_template = self.env.get_template("Kconfig.keys.jinja")
        kv_kconfig_output = self.generate_base / "Kconfig.kv_keys"

        loader = importlib.util.find_spec("infuse_iot.generated.kv_definitions")
        kv_py_template = self.env.get_template("kv_definitions.py.jinja")
        kv_py_output = pathlib.Path(loader.origin)

        with kv_def_file.open("r") as f:
            kv_defs = json.load(f)
        if self.extra_defs_base:
            kv_def_file_ext = self.extra_defs_base / "kv_store.json"
            if kv_def_file_ext.exists():
                kv_extensions_exist = True
                with kv_def_file_ext.open("r") as f:
                    kv_defs_ext = json.load(f)
                    # Ensure IDs sit in extension range
                    for kv_id, kv_def in kv_defs_ext["definitions"].items():
                        assert int(kv_id) > 32768
                        kv_def["extension"] = True
                    # Ensure no struct name collisions
                    for struct_name, struct_def in kv_defs_ext["structs"].items():
                        assert struct_name not in kv_defs["structs"]
                        struct_def["extension"] = True
                # Merge extensions into base definitions
                kv_defs["structs"].update(kv_defs_ext["structs"])
                kv_defs["definitions"].update(kv_defs_ext["definitions"])

        kv_defs["definitions"] = {int(k): v for k, v in kv_defs["definitions"].items()}
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

        with kv_kconfig_output.open("w") as f:
            f.write(kv_kconfig_template.render(definitions=kv_defs["definitions"]))

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
                    structs=kv_defs["structs"], definitions=kv_defs["definitions"]
                )
            )

        for x in ["structs", "definitions"]:
            for s in kv_defs[x].values():
                for f in s["fields"]:
                    f["py_type"] = self._py_type(f, True)

        def generate(output: pathlib.Path, extensions: bool):
            with output.open("w", encoding="utf-8") as f:
                f.write(
                    kv_py_template.render(
                        structs=kv_defs["structs"],
                        definitions=kv_defs["definitions"],
                        extensions=extensions,
                    )
                )

        generate(kv_py_output, False)
        if kv_extensions_exist:
            py_extensions = self.generate_base / "kv_definitions.py"
            generate(py_extensions, True)
            self.ruff_format(py_extensions)

        self.clang_format(kv_defs_output)
        self.ruff_format(kv_py_output)

    def rpcgen(self):
        rpc_def_file = self.definition_dir / "rpc.json"
        rpc_defs_template = self.env.get_template("rpc_types.h.jinja")
        rpc_defs_output = self.generate_base / "include" / "infuse" / "rpc" / "types.h"
        rpc_defs_output.parent.mkdir(parents=True, exist_ok=True)
        rpc_extensions_exist = False

        rpc_kconfig_template = self.env.get_template("Kconfig.commands.jinja")
        rpc_kconfig_output = self.generate_base / "Kconfig.rpc_commands"

        rpc_commands_template = self.env.get_template("rpc_commands.h.jinja")
        rpc_commands_output = (
            self.generate_base / "include" / "infuse" / "rpc" / "commands_impl.h"
        )

        rpc_runner_template = self.env.get_template("rpc_runner.c.jinja")
        rpc_runner_output = self.generate_base / "rpc_command_runner.c"

        loader = importlib.util.find_spec("infuse_iot.generated.rpc_definitions")
        rpc_defs_py_template = self.env.get_template("rpc_definitions.py.jinja")
        rpc_defs_py_output = pathlib.Path(loader.origin)

        with rpc_def_file.open("r") as f:
            rpc_defs = json.load(f)
        if self.extra_defs_base:
            rpc_def_file_ext = self.extra_defs_base / "rpc.json"
            if rpc_def_file_ext.exists():
                rpc_extensions_exist = True
                with rpc_def_file_ext.open("r") as f:
                    rpc_defs_ext = json.load(f)
                    # Ensure IDs sit in extension range
                    for rpc_id, rpc_def in rpc_defs_ext["commands"].items():
                        assert int(rpc_id) > 32768
                        rpc_def["extension"] = True
                    # Ensure no struct name collisions
                    for struct_name, struct_def in rpc_defs_ext["structs"].items():
                        assert struct_name not in rpc_defs["structs"]
                        struct_def["extension"] = True
                # Merge extensions into base definitions
                rpc_defs["structs"].update(rpc_defs_ext["structs"])
                rpc_defs["commands"].update(rpc_defs_ext["commands"])

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

        # Python type generation
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

        with rpc_kconfig_output.open("w") as f:
            f.write(rpc_kconfig_template.render(commands=rpc_defs["commands"]))

        with rpc_commands_output.open("w") as f:
            f.write(rpc_commands_template.render(commands=rpc_defs["commands"]))

        with rpc_runner_output.open("w") as f:
            f.write(rpc_runner_template.render(commands=rpc_defs["commands"]))

        with rpc_defs_output.open("w") as f:
            f.write(
                rpc_defs_template.render(
                    structs=rpc_defs["structs"],
                    enums=rpc_defs["enums"],
                    commands=rpc_defs["commands"],
                )
            )

        def generate(output: pathlib.Path, extensions: bool):
            any_enums = any(
                [
                    e.get("extension", False) == extensions
                    for e in rpc_defs["enums"].values()
                ]
            )
            with output.open("w") as f:
                f.write(
                    rpc_defs_py_template.render(
                        structs=rpc_defs["structs"],
                        enums=rpc_defs["enums"],
                        commands=rpc_defs["commands"],
                        extensions=extensions,
                        has_enums=any_enums,
                    )
                )

        generate(rpc_defs_py_output, False)
        if rpc_extensions_exist:
            py_extensions = self.generate_base / "rpc_definitions.py"
            generate(py_extensions, True)
            self.ruff_format(py_extensions)

        self.clang_format(rpc_defs_output)
        self.clang_format(rpc_commands_output)
        self.clang_format(rpc_runner_output)
        self.ruff_format(rpc_defs_py_output)
