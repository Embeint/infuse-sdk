# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import sys
from collections.abc import Generator
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter
from twister_harness.helpers.config_reader import ConfigReader

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "scripts" / "pylib"))

from infuse_pytest.dns_server import LocalDnsServer  # noqa: E402


@pytest.fixture(scope="session")
def dns_server(device_object: DeviceAdapter) -> Generator[LocalDnsServer, None, None]:
    """DNS server on the address the application was built for.

    Session scoped so that it is running before the application is launched.
    """
    config = ConfigReader(Path(device_object.device_config.app_build_dir) / "zephyr" / ".config")
    address, port = config.read("CONFIG_DNS_SERVER1").rsplit(":", 1)
    with LocalDnsServer(address=address, port=int(port)) as server:
        yield server
