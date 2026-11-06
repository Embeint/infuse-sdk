# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import sys
from collections.abc import Generator
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter
from twister_harness.helpers.config_reader import ConfigReader

sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "scripts" / "pylib"))

from infuse_pytest.coap_server import LocalCoapServer, infuse_test_resources  # noqa: E402


@pytest.fixture(scope="session")
def coap_server(device_object: DeviceAdapter) -> Generator[LocalCoapServer, None, None]:
    """CoAP over DTLS server on the address the application was built for.

    Session scoped so that it is running before the application is launched.
    """
    config = ConfigReader(Path(device_object.device_config.app_build_dir) / "zephyr" / ".config")
    with LocalCoapServer(
        address=config.read("CONFIG_TEST_COAP_SERVER"),
        port=config.read_int("CONFIG_TEST_COAP_SERVER_PORT"),
        resources=infuse_test_resources(),
        dtls=True,
    ) as server:
        yield server
