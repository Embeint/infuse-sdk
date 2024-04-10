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
        template = self.env.get_template('tdf_definitions.h.jinja')
        tdf_def_file = self.template_dir / 'tdf.json'
        output_file = self.eis_root_dir / 'include' / 'eis' / 'tdf' / 'definitions.h'

        with tdf_def_file.open('r') as f:
            tdf_defs = json.load(f)

        with output_file.open('w') as f:
            f.write(template.render(structs=tdf_defs['structs'], definitions=tdf_defs['definitions']))
            f.write(os.linesep)
