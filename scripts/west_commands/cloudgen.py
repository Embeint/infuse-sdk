# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Holdings Pty Ltd

import argparse
import copy
import importlib
import json
import pathlib
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
        parser.add_argument("--defs", "-d", type=str, help="Folder containing extentsion definitions")
        parser.add_argument(
            "--skip-python-generation",
            "--skip-python",
            action="store_true",
            help="Skip generation of Python definition files",
        )
        return parser

    def do_run(self, args, unknown_args):
        self.extra_defs_base = pathlib.Path(args.defs) if args.defs else None
        self.output_base = pathlib.Path(args.output)
        self.skip_python_generation = args.skip_python_generation
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
        self.tasksgen()

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
                if "counted_by" in field:
                    d["counted_by"] = field["counted_by"]
            else:
                field["array"] = f"[{field['num']}]"

    def _task_type_name(self, ctype: str, kind: str):
        prefix = f"{kind} "
        return ctype.removeprefix(prefix) if ctype.startswith(prefix) else None

    def _task_enum_local_name(self, task_name: str, enum_name: str):
        prefix = "schedule_enum_task_"
        suffix = enum_name.removeprefix(prefix) if enum_name.startswith(prefix) else enum_name
        task_prefix = f"{task_name.lower()}_"
        if suffix.startswith(task_prefix):
            suffix = suffix.removeprefix(task_prefix)
        return f"task_{task_name.lower()}_{suffix}"

    def _task_value_expr(self, value):
        if "bit" in value:
            return f"BIT({value['bit']})"
        return value["value"]

    def _task_py_value_expr(self, value):
        if "bit" in value:
            return f"BIT({value['bit']})"
        if value["value"] == 0 and value["name"].endswith("_MODE"):
            return "0x00"
        return value["value"]

    def _snake_to_pascal(self, name: str):
        return "".join(part.capitalize() for part in name.lower().split("_"))

    def _task_py_class(self, task_name: str):
        return f"Task{self._snake_to_pascal(task_name)}"

    def _task_py_member_class(self, type_name: str, task_name: str):
        task_base = task_name.lower()
        for prefix in [
            f"schedule_struct_task_{task_base}_",
            f"schedule_union_task_{task_base}_",
        ]:
            if type_name.startswith(prefix):
                type_name = type_name.removeprefix(prefix)
                break
        if type_name.endswith("_args"):
            type_name = type_name.removesuffix("_args")
        return f"{self._snake_to_pascal(type_name)}Args"

    def _task_py_enum_class(self, enum_name: str, task_name: str, field_name: str | None = None):
        if field_name == "flags":
            return "Flags"
        if field_name == "tdfs":
            return "Tdfs"
        local_name = self._task_enum_local_name(task_name, enum_name)
        suffix = local_name.removeprefix(f"task_{task_name.lower()}_")
        if suffix == "logs":
            return "Logging"
        if suffix == "flags":
            return "Flags"
        if suffix.endswith("_flags"):
            suffix = suffix.removesuffix("_flags")
        return self._snake_to_pascal(suffix)

    def _task_py_type(self, field, task_name: str, task_defs):
        py_type_field = copy.copy(field)
        py_type_field.pop("num", None)
        struct_name = self._task_type_name(field["type"], "struct")
        if struct_name:
            base = f"{self._task_py_class(task_name)}.{self._task_py_member_class(struct_name, task_name)}"
        else:
            union_name = self._task_type_name(field["type"], "union")
            if union_name:
                base = f"{self._task_py_class(task_name)}.{self._task_py_member_class(union_name, task_name)}"
            else:
                enum_name = self._task_type_name(field["type"], "enum")
                if enum_name:
                    py_type_field["type"] = task_defs["enums"][enum_name]["type"]
                base = self._py_type(py_type_field, False)

        if "num" in field:
            return f"{field['num']} * {base}"
        return base

    def _task_prepare_enum(self, enum_info):
        for value in enum_info["values"]:
            value["value_expr"] = self._task_value_expr(value)
            value["py_value_expr"] = self._task_py_value_expr(value)

    def _task_log_enum_name(self, task_name: str, task_defs):
        task_base = task_name.lower()
        enum_name = f"schedule_enum_task_{task_base}_logs"
        if enum_name in task_defs["enums"]:
            return enum_name

        if "_alt" in task_base:
            task_base = task_base.rsplit("_alt", 1)[0]
            enum_name = f"schedule_enum_task_{task_base}_logs"
            if enum_name in task_defs["enums"]:
                return enum_name

        return None

    def _task_log_defines(self, task_name: str, task_defs):
        enum_name = self._task_log_enum_name(task_name, task_defs)
        if enum_name is None:
            return []

        return [
            {
                "name": f"TASK_{task_name.upper()}_LOG_{value['name']}",
                "value": self._task_value_expr(value),
                "description": value.get("description", ""),
            }
            for value in task_defs["enums"][enum_name]["values"]
        ]

    def _task_flag_defines(self, task_name: str, task_defs, skip_enums):
        prefix = f"schedule_enum_task_{task_name.lower()}_"
        defines = []
        for enum_name, enum_info in task_defs["enums"].items():
            if not enum_name.startswith(prefix) or enum_name.endswith("_logs") or enum_name in skip_enums:
                continue
            local_name = self._task_enum_local_name(task_name, enum_name)
            value_prefix = local_name.upper()
            for value in enum_info["values"]:
                defines.append(
                    {
                        "name": f"{value_prefix}_{value['name']}",
                        "value": self._task_value_expr(value),
                        "description": value.get("description", ""),
                    }
                )
        return defines

    def _task_prepare_field(self, field, task_name: str, task_defs, task_enums):
        field["render_type"] = field["type"]
        self._array_postfix({}, field)

        enum_name = self._task_type_name(field["type"], "enum")
        if enum_name:
            enum_info = task_defs["enums"][enum_name]
            field["render_type"] = enum_info["type"]
            if enum_name not in task_enums:
                local_name = self._task_enum_local_name(task_name, enum_name)
                task_enums[enum_name] = copy.deepcopy(enum_info)
                task_enums[enum_name]["local_name"] = local_name
                task_enums[enum_name]["value_prefix"] = local_name.upper()
                self._task_prepare_enum(task_enums[enum_name])

    def tasksgen(self):
        task_def_file = self.definition_dir / "tasks.json"
        task_args_template = self.env.get_template("task_args.h.jinja")
        task_ids_template = self.env.get_template("task_ids.h.jinja")
        infuse_task_args_template = self.env.get_template("infuse_task_args.h.jinja")
        infuse_tasks_template = self.env.get_template("infuse_tasks.h.jinja")
        task_definitions_template = self.env.get_template("task_definitions.py.jinja")
        task_args_output_base = self.generate_base / "include" / "infuse" / "task_runner" / "tasks"
        task_args_always_output_base = self.generate_base / "include_always" / "infuse" / "task_runner" / "tasks"
        task_args_output_base.mkdir(parents=True, exist_ok=True)
        task_args_always_output_base.mkdir(parents=True, exist_ok=True)
        task_ids_output = task_args_output_base / "infuse_task_ids.h"
        infuse_task_args_output = task_args_output_base / "infuse_task_args.h"
        infuse_tasks_output = task_args_output_base / "infuse_tasks.h"

        with task_def_file.open("r") as f:
            task_defs = json.load(f)

        task_defs.setdefault("structs", {})
        task_defs.setdefault("unions", {})
        task_defs.setdefault("enums", {})
        task_defs.setdefault("definitions", {})
        if "include_namespace" not in task_defs:
            sys.exit(f"Missing include_namespace in '{task_def_file}'")
        include_namespace = task_defs["include_namespace"]
        for task in task_defs["definitions"].values():
            task["include_namespace"] = include_namespace
        custom_task_ids = set()

        if self.extra_defs_base:
            task_def_file_ext = self.extra_defs_base / "tasks.json"
            if task_def_file_ext.exists():
                with task_def_file_ext.open("r") as f:
                    task_defs_ext = json.load(f)
                if "include_namespace" not in task_defs_ext:
                    sys.exit(f"Missing include_namespace in '{task_def_file_ext}'")
                for key in ["structs", "unions", "enums", "definitions"]:
                    task_defs_ext.setdefault(key, {})
                    for name in task_defs_ext[key]:
                        assert name not in task_defs[key]
                include_namespace_ext = task_defs_ext["include_namespace"]
                for task in task_defs_ext["definitions"].values():
                    task["include_namespace"] = include_namespace_ext
                for key in ["structs", "unions", "enums", "definitions"]:
                    task_defs[key].update(task_defs_ext[key])
                custom_task_ids = {int(task_id) for task_id in task_defs_ext["definitions"]}

        task_defs["definitions"] = dict(sorted((int(k), v) for k, v in task_defs["definitions"].items()))
        task_ids = {}
        for task_id, task in task_defs["definitions"].items():
            assert task_id not in task_ids, f"Duplicate task ID {task_id}"
            task_ids[task_id] = task
            for alt_id, alt in task.get("alternate_ids", {}).items():
                alt_id = int(alt_id)
                assert alt_id not in task_ids, f"Duplicate task ID {alt_id}"
                task_ids[alt_id] = alt
        task_ids = dict(sorted(task_ids.items()))

        with task_ids_output.open("w") as f:
            f.write(task_ids_template.render(tasks=task_ids))
        self.clang_format(task_ids_output)

        with infuse_task_args_output.open("w") as f:
            f.write(infuse_task_args_template.render(tasks=task_defs["definitions"]))
        self.clang_format(infuse_task_args_output)

        with infuse_tasks_output.open("w") as f:
            f.write(infuse_tasks_template.render(tasks=task_defs["definitions"]))
        self.clang_format(infuse_tasks_output)

        def collect_type(ctype, task_name, task_structs, task_unions, task_enums):
            struct_name = self._task_type_name(ctype, "struct")
            if struct_name:
                if struct_name in task_structs:
                    return
                struct_info = copy.deepcopy(task_defs["structs"][struct_name])
                for field in struct_info["fields"]:
                    collect_type(field["type"], task_name, task_structs, task_unions, task_enums)
                    self._task_prepare_field(field, task_name, task_defs, task_enums)
                task_structs[struct_name] = struct_info
                return

            union_name = self._task_type_name(ctype, "union")
            if union_name:
                if union_name in task_unions:
                    return
                union_info = copy.deepcopy(task_defs["unions"][union_name])
                for field in union_info["fields"]:
                    collect_type(field["type"], task_name, task_structs, task_unions, task_enums)
                    self._task_prepare_field(field, task_name, task_defs, task_enums)
                task_unions[union_name] = union_info
                return

            enum_name = self._task_type_name(ctype, "enum")
            if enum_name and enum_name not in task_enums:
                enum_info = copy.deepcopy(task_defs["enums"][enum_name])
                local_name = self._task_enum_local_name(task_name, enum_name)
                enum_info["local_name"] = local_name
                enum_info["value_prefix"] = local_name.upper()
                self._task_prepare_enum(enum_info)
                task_enums[enum_name] = enum_info

        python_tasks = []
        for task_id, task in task_defs["definitions"].items():
            task = copy.deepcopy(task)
            task_structs = {}
            task_unions = {}
            task_enums = {}

            for field in task["fields"]:
                collect_type(field["type"], task["name"], task_structs, task_unions, task_enums)
                self._task_prepare_field(field, task["name"], task_defs, task_enums)

            if not custom_task_ids or task_id in custom_task_ids:
                output_base = task_args_output_base if custom_task_ids else task_args_always_output_base
                output = output_base / f"{task['name'].lower()}_args.h"
                include_guard = f"INFUSE_SDK_INCLUDE_GENERATED_TASK_RUNNER_TASKS_{task['name'].upper()}_ARGS_H_"
                with output.open("w") as f:
                    f.write(
                        task_args_template.render(
                            task=task,
                            structs=task_structs,
                            unions=task_unions,
                            enums=task_enums.values(),
                            log_defines=self._task_log_defines(task["name"], task_defs),
                            flag_defines=self._task_flag_defines(task["name"], task_defs, task_enums),
                            include_guard=include_guard,
                        )
                    )
                self.clang_format(output)

            task["id"] = task_id
            task["class_name"] = self._task_py_class(task["name"])
            task["anonymous_fields"] = [field["name"] for field in task["fields"] if field["type"].startswith("union ")]
            task["py_enums"] = []
            seen_py_enum_classes = set()
            task["logging_class_name"] = None
            log_enum_name = self._task_log_enum_name(task["name"], task_defs)
            if log_enum_name:
                enum_info = copy.deepcopy(task_defs["enums"][log_enum_name])
                enum_info["class_name"] = "Tdfs" if task["name"] == "TDF_LOGGER" else "Logging"
                self._task_prepare_enum(enum_info)
                task["py_enums"].append(enum_info)
                seen_py_enum_classes.add(enum_info["class_name"])
                task["logging_class_name"] = enum_info["class_name"]
            for field in task["fields"]:
                field["py_type"] = self._task_py_type(field, task["name"], task_defs)
                enum_name = self._task_type_name(field["type"], "enum")
                if enum_name:
                    enum_info = copy.deepcopy(task_defs["enums"][enum_name])
                    enum_info["class_name"] = self._task_py_enum_class(enum_name, task["name"], field["name"])
                    if enum_info["class_name"] not in seen_py_enum_classes:
                        self._task_prepare_enum(enum_info)
                        task["py_enums"].append(enum_info)
                        seen_py_enum_classes.add(enum_info["class_name"])
            for idx, (alt_id, _alt) in enumerate(task.get("alternate_ids", {}).items(), 1):
                task[f"alt{idx}_id"] = int(alt_id)

            task["structs"] = task_structs
            for name, info in task["structs"].items():
                info["class_name"] = self._task_py_member_class(name, task["name"])
                info["py_enums"] = []
                for field in info["fields"]:
                    field["py_type"] = self._task_py_type(field, task["name"], task_defs)
                    enum_name = self._task_type_name(field["type"], "enum")
                    if enum_name:
                        enum_info = copy.deepcopy(task_defs["enums"][enum_name])
                        enum_info["class_name"] = self._task_py_enum_class(enum_name, task["name"], field["name"])
                        self._task_prepare_enum(enum_info)
                        info["py_enums"].append(enum_info)

            task["unions"] = task_unions
            for name, info in task["unions"].items():
                info["class_name"] = self._task_py_member_class(name, task["name"])
                for field in info["fields"]:
                    field["py_type"] = self._task_py_type(field, task["name"], task_defs)

            python_tasks.append(task)

        if self.skip_python_generation:
            return

        loader = importlib.util.find_spec("infuse_iot")
        if loader is None or loader.submodule_search_locations is None:
            sys.exit("Unable to locate infuse_iot package")
        task_definitions_output = pathlib.Path(next(iter(loader.submodule_search_locations))) / "generated" / "tasks.py"

        with task_definitions_output.open("w", encoding="utf-8") as f:
            f.write(task_definitions_template.render(tasks=python_tasks))
        self.ruff_format(task_definitions_output)

    def tdfgen(self):
        tdf_def_file = self.definition_dir / "tdf.json"
        tdf_template = self.env.get_template("tdf_definitions.h.jinja")
        tdf_output = self.generate_base / "include" / "infuse" / "tdf" / "definitions.h"
        tdf_output.parent.mkdir(parents=True, exist_ok=True)

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
            d["only_flexible"] = len(d["fields"]) == 1 and d["fields"][0]["array"] == "[]"

        with tdf_output.open("w") as f:
            f.write(tdf_template.render(structs=tdf_defs["structs"], definitions=tdf_defs["definitions"]))

        self.clang_format(tdf_output)

        if self.skip_python_generation:
            return

        def conv_formula(f):
            conv = f"self._{f['name']}"
            h = f["conversion"].get("hex", None)
            i = f["conversion"].get("int", None)
            m = f["conversion"].get("m", 1)
            c = f["conversion"].get("c", 0)

            if h is not None:
                conv = f"bytes({conv}).hex()"
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
                    fmt = f'"0x{{:0{digits}x}}"'
                else:
                    fmt = '"0x{:x}"'  # noqa : SIM102
            if d.get("fmt") == "float" and digits:
                fmt = f'"{{:.{digits}f}}"'  # noqa : SIM102
            p = d.get("postfix", "")
            return {"name": f["name"], "fmt": fmt, "postfix": f'"{p}"'}

        loader = importlib.util.find_spec("infuse_iot.generated.tdf_definitions")
        tdf_definitions_template = self.env.get_template("tdf_definitions.py.jinja")
        tdf_definitions_output = pathlib.Path(loader.origin)

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

        self.ruff_format(tdf_definitions_output)

    def kvgen(self):
        kv_def_file = self.definition_dir / "kv_store.json"
        kv_defs_template = self.env.get_template("kv_types.h.jinja")
        kv_defs_output = self.generate_base / "include" / "infuse" / "fs" / "kv_types.h"
        kv_defs_output.parent.mkdir(parents=True, exist_ok=True)
        kv_extensions_exist = False

        kv_kconfig_template = self.env.get_template("Kconfig.keys.jinja")
        kv_kconfig_output = self.generate_base / "Kconfig.kv_keys"

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
            def array_postfix(name, d, field):
                field["array"] = ""
                if "num" in field:
                    if field["num"] == 0:
                        if len(d["fields"]) == 1:
                            err = f"KV definition '{name}' contains only a single VLA element. "
                            err += "Due to C language limitations, it must contain at least one non-VLA element."
                            sys.exit(err)
                        field["array"] = "[]"
                        field["flexible"] = True
                        d["flexible"] = True
                    else:
                        field["array"] = f"[{field['num']}]"

            for name, d in kv_defs["structs"].items():
                for field in d["fields"]:
                    array_postfix(name, d, field)
            for d in kv_defs["definitions"].values():
                for field in d["fields"]:
                    array_postfix(d["name"], d, field)
                    # If contained struct is flexible, so is this struct
                    if field["type"].startswith("struct "):
                        s = field["type"].removeprefix("struct ")
                        if kv_defs["structs"][s].get("flexible", False):
                            field["flexible_type"] = s
                            d["flexible"] = True

            f.write(kv_defs_template.render(structs=kv_defs["structs"], definitions=kv_defs["definitions"]))

        self.clang_format(kv_defs_output)

        if self.skip_python_generation:
            return

        loader = importlib.util.find_spec("infuse_iot.generated.kv_definitions")
        kv_py_template = self.env.get_template("kv_definitions.py.jinja")
        kv_py_output = pathlib.Path(loader.origin)

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
        rpc_commands_output = self.generate_base / "include" / "infuse" / "rpc" / "commands_impl.h"

        rpc_runner_template = self.env.get_template("rpc_runner.c.jinja")
        rpc_runner_output = self.generate_base / "rpc_command_runner.c"

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

        self.clang_format(rpc_defs_output)
        self.clang_format(rpc_commands_output)
        self.clang_format(rpc_runner_output)

        if self.skip_python_generation:
            return

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

        loader = importlib.util.find_spec("infuse_iot.generated.rpc_definitions")
        rpc_defs_py_template = self.env.get_template("rpc_definitions.py.jinja")
        rpc_defs_py_output = pathlib.Path(loader.origin)

        def generate(output: pathlib.Path, extensions: bool):
            any_enums = any([e.get("extension", False) == extensions for e in rpc_defs["enums"].values()])
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

        self.ruff_format(rpc_defs_py_output)
