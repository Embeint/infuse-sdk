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

from infuse_pytest.sntp_server import LocalSntpServer  # noqa: E402


@pytest.fixture(scope="session")
def sntp_server(device_object: DeviceAdapter) -> Generator[LocalSntpServer, None, None]:
    """SNTP server on the port the application was built for.

    Session scoped so that it is running before the application is launched.
    """
    config = ConfigReader(Path(device_object.device_config.app_build_dir) / "zephyr" / ".config")
    with LocalSntpServer(
        port=config.read_int("CONFIG_SNTP_AUTO_SERVER_PORT"),
        responsive_addr=config.read("CONFIG_SNTP_AUTO_DEFAULT_SERVER"),
    ) as server:
        yield server
