# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2024 Embeint Pty Ltd

import argparse
import pathlib
import json
import os

from jinja2 import Environment, FileSystemLoader, select_autoescape

from west.commands import WestCommand

EXPORT_DESCRIPTION = '''\
This command generates code from common definitions in Embeint cloud.
'''

class cloudgen(WestCommand):
    def __init__(self):
        super().__init__(
            'cloudgen',
            # Keep this in sync with the string in west-commands.yml.
            'generate files from EIS cloud definitions',
            EXPORT_DESCRIPTION,
            accepts_unknown_args=False)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description)
        return parser

    def do_run(self, args, unknown_args):
        self.template_dir = pathlib.Path(__file__).parent / 'templates'
        self.eis_root_dir = pathlib.Path(__file__).parent.parent.parent
        self.env = Environment(
            loader = FileSystemLoader(self.template_dir),
            autoescape=select_autoescape(),
            trim_blocks = True,
            lstrip_blocks = True,
        )
        self.tdfgen()

    def tdfgen(self):
        tdf_def_file = self.template_dir / 'tdf.json'
        tdf_template = self.env.get_template('tdf_definitions.h.jinja')
        tdf_output = self.eis_root_dir / 'include' / 'eis' / 'tdf' / 'definitions.h'

        kv_def_file = self.template_dir / 'kv_store.json'
        kv_defs_template = self.env.get_template('kv_types.h.jinja')
        kv_defs_output = self.eis_root_dir / 'include' / 'eis' / 'fs' / 'kv_types.h'

        kv_kconfig_template = self.env.get_template('Kconfig.keys.jinja')
        kv_kconfig_output = self.eis_root_dir / 'subsys' / 'fs' / 'kv_store' / 'Kconfig.keys'

        kv_keys_template = self.env.get_template('kv_keys.c.jinja')
        kv_keys_output = self.eis_root_dir / 'subsys' / 'fs' / 'kv_store' / 'kv_keys.c'

        with tdf_def_file.open('r') as f:
            tdf_defs = json.load(f)

        with tdf_output.open('w') as f:
            f.write(tdf_template.render(structs=tdf_defs['structs'], definitions=tdf_defs['definitions']))
            f.write(os.linesep)

        with kv_def_file.open('r') as f:
            kv_defs = json.load(f)

        with kv_kconfig_output.open('w') as f:
            f.write(kv_kconfig_template.render(definitions=kv_defs['definitions']))

        with kv_keys_output.open('w') as f:
            for d in kv_defs['definitions']:
                flags = []
                if d.get('reflect', False):
                    flags.append('KV_FLAGS_REFLECT')
                if d.get('readback_protection', False):
                    flags.append('KV_FLAGS_WRITE_ONLY')
                if len(flags) > 0:
                    d['flags'] = ' | '.join(flags)
                else:
                    d['flags'] = 0
            f.write(kv_keys_template.render(definitions=kv_defs['definitions']))
            f.write(os.linesep)

        with kv_defs_output.open('w') as f:
            # Simplify template logic for array postfix
            def array_postfix(d, field):
                field['array'] = ''
                if 'num' in field:
                    if field['num'] == 0:
                        field['array'] = '[]'
                        d['flexible'] = True
                    else:
                        field['array'] = f"[{field['num']}]"

            for d in kv_defs['structs'].values():
                for field in d['fields']:
                    array_postfix(d, field)
            for d in kv_defs['definitions']:
                for field in d['fields']:
                    array_postfix(d, field)
                    # If contained struct is flexible, so is this struct
                    if field['type'].startswith('struct '):
                        s = field['type'].removeprefix('struct ')
                        if kv_defs['structs'][s].get('flexible', False):
                            field['flexible'] = s
                            d['flexible'] = True

            f.write(kv_defs_template.render(structs=kv_defs['structs'], definitions=kv_defs['definitions']))
            f.write(os.linesep)
