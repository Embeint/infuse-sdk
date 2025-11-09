#!/usr/bin/env python3
import argparse
import datetime
import pathlib
import sys

import openhtf
from lib_config import ValidationConfig
from lib_exceptions import ConfigurationError
from lib_loaders import (
    load_default_tests,
)
from lib_phases import (
    dut_id_query,
    operator_id_query,
    retrieve_operator_id,
)
from openhtf.output.callbacks import json_factory
from openhtf.output.servers import station_server
from openhtf.util import configuration

ONE_DAY = 60 * 60 * 24

CONF = configuration.CONF
CONF.load(station_server_port=4444, _allow_undeclared=True)
CONF.declare("general")
CONF.declare("tests")
CONF.declare("infuse")
CONF.declare("socs")


def test_record_filename(**kwargs):
    now = datetime.datetime.now(datetime.timezone.utc)

    folder = (
        pathlib.Path(CONF.general["result-folder"])
        / kwargs["station_id"]
        / str(now.year)
        / str(now.month)
        / str(now.day)
    )
    fname = f"{now.hour:02d}{now.minute:02d}{now.second:02d}_{kwargs['dut_id']}.json"
    if kwargs["outcome"] != "PASS":
        fname = "FAILURE_" + fname
    # Ensure folder exists
    folder.mkdir(parents=True, exist_ok=True)
    return str(folder / fname)


def run_tests(test: openhtf.Test, config: ValidationConfig) -> None:
    with station_server.StationServer() as server:
        test.add_output_callbacks(server.publish_final_state)

        # Output records to JSON
        test.add_output_callbacks(json_factory.OutputToJSON(test_record_filename, indent=4))

        print("🚀 Operator GUI running at: http://localhost:4444/")
        print("📋 JSON results stored in ./records/")

        if CONF.general.get("operator-id", False):
            test_get_op = openhtf.Test(operator_id_query)
            test_get_op.configure(name="Get Operator ID")
            while not test_get_op.execute():
                continue
            config.operator = retrieve_operator_id()
        while True:
            # Wait for operator to start test via GUI
            test.execute(test_start=dut_id_query.with_args(config=config))


def main():
    parser = argparse.ArgumentParser(
        "OpenHTF validation server", parents=[configuration.ARG_PARSER], allow_abbrev=False
    )
    parser.add_argument(
        "--infuse-api-key",
        type=str,
        help="Infuse-IoT API Key for provisioning (without Bearer prefix)",
    )
    args = parser.parse_args()

    try:
        # Load, parse, and validate configuration file
        config = ValidationConfig(args, CONF)
        # Create tests to implement the configuration
        test = load_default_tests(config)
        test.configure(name=CONF.general["test-title"])
    except ConfigurationError as e:
        sys.exit(e)
    # Run tests until terminated
    run_tests(test, config)


if __name__ == "__main__":
    main()
