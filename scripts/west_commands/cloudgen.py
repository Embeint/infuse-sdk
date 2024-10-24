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
        return parser

    def do_run(self, args, unknown_args):
        self.template_dir = pathlib.Path(__file__).parent / "templates"
        self.infuse_root_dir = pathlib.Path(__file__).parent.parent.parent
        self.env = Environment(
            loader=FileSystemLoader(self.template_dir),
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
            f'--style=file:{self.infuse_root_dir/".clang-format"}',
            str(file),
        ]
        subprocess.run(args)

    def tdfgen(self):
        tdf_def_file = self.template_dir / "tdf.json"
        tdf_template = self.env.get_template("tdf_definitions.h.jinja")
        tdf_output = (
            self.infuse_root_dir / "include" / "infuse" / "tdf" / "definitions.h"
        )

        loader = importlib.util.find_spec("infuse_iot.generated.tdf_definitions")
        tdf_definitions_template = self.env.get_template("tdf_definitions.py.jinja")
        tdf_definitions_output = pathlib.Path(loader.origin)

        with tdf_def_file.open("r") as f:
            tdf_defs = json.load(f)

        with tdf_output.open("w") as f:
            f.write(
                tdf_template.render(
                    structs=tdf_defs["structs"], definitions=tdf_defs["definitions"]
                )
            )
            f.write(os.linesep)

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
        }

        def conv_formula(f):
            p = ""
            if f["conversion"]["m"] != 1:
                p += f" * {f['conversion']['m']}"
            if f["conversion"]["c"] != 0:
                p += f" + {f['conversion']['c']}"
            return {"name": f["name"], "conv": p}

        def py_type(f):
            if "num" in f:
                return f'{f["num"]} * {ctype_mapping[f["type"]]}'
            else:
                return ctype_mapping[f["type"]]

        for x in ["structs", "definitions"]:
            for s in tdf_defs[x].values():
                s["conversions"] = []
                for f in s["fields"]:
                    if "conversion" in f:
                        f["py_name"] = f"_{f['name']}"
                        s["conversions"].append(conv_formula(f))
                    else:
                        f["py_name"] = f["name"]

                    t: str = f["type"]
                    if t.startswith("struct"):
                        f["py_type"] = f"structs.{t[7:]}"
                    else:
                        f["py_type"] = py_type(f)

        with tdf_definitions_output.open("w", encoding="utf-8") as f:
            f.write(
                tdf_definitions_template.render(
                    structs=tdf_defs["structs"], definitions=tdf_defs["definitions"]
                )
            )
            f.write(os.linesep)

        self.clang_format(tdf_output)

    def kvgen(self):
        kv_def_file = self.template_dir / "kv_store.json"
        kv_defs_template = self.env.get_template("kv_types.h.jinja")
        kv_defs_output = (
            self.infuse_root_dir / "include" / "infuse" / "fs" / "kv_types.h"
        )

        kv_kconfig_template = self.env.get_template("Kconfig.keys.jinja")
        kv_kconfig_output = (
            self.infuse_root_dir / "subsys" / "fs" / "kv_store" / "Kconfig.keys"
        )

        kv_keys_template = self.env.get_template("kv_keys.c.jinja")
        kv_keys_output = (
            self.infuse_root_dir / "subsys" / "fs" / "kv_store" / "kv_keys.c"
        )

        with kv_def_file.open("r") as f:
            kv_defs = json.load(f)

        with kv_kconfig_output.open("w") as f:
            f.write(kv_kconfig_template.render(definitions=kv_defs["definitions"]))

        with kv_keys_output.open("w") as f:
            for d in kv_defs["definitions"]:
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

        with kv_defs_output.open("w") as f:
            # Simplify template logic for array postfix
            def array_postfix(d, field):
                field["array"] = ""
                if "num" in field:
                    if field["num"] == 0:
                        field["array"] = "[]"
                        d["flexible"] = True
                    else:
                        field["array"] = f"[{field['num']}]"

            for d in kv_defs["structs"].values():
                for field in d["fields"]:
                    array_postfix(d, field)
            for d in kv_defs["definitions"]:
                for field in d["fields"]:
                    array_postfix(d, field)
                    # If contained struct is flexible, so is this struct
                    if field["type"].startswith("struct "):
                        s = field["type"].removeprefix("struct ")
                        if kv_defs["structs"][s].get("flexible", False):
                            field["flexible"] = s
                            d["flexible"] = True

            f.write(
                kv_defs_template.render(
                    structs=kv_defs["structs"], definitions=kv_defs["definitions"]
                )
            )
            f.write(os.linesep)

        self.clang_format(kv_defs_output)
        self.clang_format(kv_keys_output)

    def rpcgen(self):
        rpc_def_file = self.template_dir / "rpc.json"
        rpc_defs_template = self.env.get_template("rpc_types.h.jinja")
        rpc_defs_output = (
            self.infuse_root_dir / "include" / "infuse" / "rpc" / "types.h"
        )

        rpc_kconfig_template = self.env.get_template("Kconfig.commands.jinja")
        rpc_kconfig_output = (
            self.infuse_root_dir / "subsys" / "rpc" / "commands" / "Kconfig.commands"
        )

        rpc_commands_template = self.env.get_template("rpc_commands.h.jinja")
        rpc_commands_output = (
            self.infuse_root_dir / "subsys" / "rpc" / "commands" / "commands.h"
        )

        rpc_runner_template = self.env.get_template("rpc_runner.c.jinja")
        rpc_runner_output = self.infuse_root_dir / "subsys" / "rpc" / "command_runner.c"

        loader = importlib.util.find_spec("infuse_iot.generated.rpc_definitions")
        rpc_defs_py_template = self.env.get_template("rpc_definitions.py.jinja")
        rpc_defs_py_output = pathlib.Path(loader.origin)

        with rpc_def_file.open("r") as f:
            rpc_defs = json.load(f)

        with rpc_kconfig_output.open("w") as f:
            f.write(rpc_kconfig_template.render(commands=rpc_defs["commands"]))

        with rpc_commands_output.open("w") as f:
            f.write(rpc_commands_template.render(commands=rpc_defs["commands"]))
            f.write(os.linesep)

        with rpc_runner_output.open("w") as f:
            f.write(rpc_runner_template.render(commands=rpc_defs["commands"]))
            f.write(os.linesep)

        with rpc_defs_output.open("w") as f:
            # Simplify template logic for array postfix
            def array_postfix(d, field):
                field["array"] = ""
                if "num" in field:
                    if field["num"] == 0:
                        field["array"] = "[]"
                        d["flexible"] = True
                    else:
                        field["array"] = f"[{field['num']}]"

            for d in rpc_defs["structs"].values():
                for field in d["fields"]:
                    array_postfix(d, field)
            for d in rpc_defs["commands"].values():
                for field in d["request_params"]:
                    array_postfix(d, field)
                for field in d["response_params"]:
                    array_postfix(d, field)

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
                    structs=rpc_defs["structs"],
                    enums=rpc_defs["enums"],
                    commands=rpc_defs["commands"],
                )
            )
            f.write(os.linesep)

        with rpc_defs_py_output.open("w") as f:
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

            def py_type(field):
                if "num" in field:
                    return f'{field["num"]} * {ctype_mapping[field["type"]]}'
                else:
                    return ctype_mapping[field["type"]]

            for s in rpc_defs["structs"].values():
                for field in s["fields"]:
                    field["py_name"] = field["name"]

                    t: str = field["type"]
                    if t.startswith("struct"):
                        field["py_type"] = f"{t[7:]}"
                    else:
                        field["py_type"] = py_type(field)
            for e in rpc_defs["enums"].values():
                for value in e["values"]:
                    value["py_name"] = value["name"]

            f.write(
                rpc_defs_py_template.render(
                    structs=rpc_defs["structs"],
                    enums=rpc_defs["enums"],
                    commands=rpc_defs["commands"],
                )
            )
            f.write(os.linesep)

        self.clang_format(rpc_defs_output)
        self.clang_format(rpc_commands_output)
        self.clang_format(rpc_runner_output)
