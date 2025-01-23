#!/usr/bin/env python3
import os
import random
import yaml
from west.commands import WestCommand  # Import WestCommand base class

EXPORT_DESCRIPTION = """\
This command generates a network key file for Infuse-IoT.
"""

class network_keygen(WestCommand):
    def __init__(self):
        super().__init__(
            name='network-keygen',
            # Keep this in sync with the string in west-commands.yml.
            help="generate a Infuse Network Key YAML file",
            description=EXPORT_DESCRIPTION,
            accepts_unknown_args=False,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
        )

        parser.add_argument('-o', '--output', type=str, default='customer_network.yaml', help='Output YAML file (default: customer_network.yaml)')
        parser.add_argument('-i', '--id', type=str, help='Set a custom ID as an integer or hexadecimal (default: random ID)')
        return parser

    def do_run(self, args, unknown_args):
        # Call the generate_key_yaml function with the arguments provided
        generate_key_yaml(args.output, args.id)

def generate_random_id():
    """Generate a random integer ID in the range of 0x100000 to 0xFFFFFF."""
    return random.randint(0x100000, 0xFFFFFF)

def generate_key_yaml(output_file, id_value=None):
    # If the ID is provided, ensure it is an integer
    if id_value:
        try:
            id_value = int(id_value, 0)  # Interpret the value as an integer, supporting hex (0x)
        except ValueError:
            raise ValueError("Provided ID must be a valid integer or hexadecimal number.")
    else:
        # Generate a random ID if not provided
        id_value = generate_random_id()

    # Generate a 256-bit key (32 bytes)
    key = os.urandom(32)

    # Prepare the YAML structure
    data = {
        'id': id_value,
        'key': key
    }

    # Write to the specified YAML file
    with open(output_file, 'w') as file:
        yaml.dump(data, file, default_flow_style=False, sort_keys=False)

    print(f"YAML file '{output_file}' has been generated with ID: {id_value:#x}")

