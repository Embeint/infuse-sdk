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

from infuse_pytest.coap_server import (  # noqa: E402
    CODE_CONTENT,
    CODE_METHOD_NOT_ALLOWED,
    CODE_UNAUTHORIZED,
    CoapResource,
    LocalCoapServer,
)

# Delay before the separate response to the `separate` resource
SEPARATE_DELAY = 3.0

# Content of `large`, must match `large_data` in src/main.c
LARGE = bytes((i * 31 + 7) & 0xFF for i in range(1700))

# Subset of the resources of the coap.me test server used by the application
RESOURCES = {
    "secret": CoapResource(code=CODE_UNAUTHORIZED),
    "location-query": CoapResource(code=CODE_METHOD_NOT_ALLOWED),
    "hello": b"world",
    "seg1/seg2/seg3": b"Matroshka",
    "large": LARGE,
    "separate": CoapResource(payload=b"That took a long time", code=CODE_CONTENT, separate=SEPARATE_DELAY),
}

# Resources only provided by the local server, to test client behaviour
RESOURCES.update(
    {
        "empty": b"",
        "large/no_size2": CoapResource(payload=LARGE, size2=False),
        "large/reduced": CoapResource(payload=LARGE, max_szx=5),
        "large/wrong_block": CoapResource(payload=LARGE, block_offset=1),
        "large/oversized": CoapResource(payload=LARGE, extra_payload=16),
        "hello/long_token": CoapResource(payload=b"world", token_len=8),
        "hello/short_token": CoapResource(payload=b"world", token_len=2),
    }
)


@pytest.fixture(scope="session")
def coap_server(device_object: DeviceAdapter) -> Generator[LocalCoapServer, None, None]:
    """CoAP server on the address the application was built for.

    Session scoped so that it is running before the application is launched.
    """
    config = ConfigReader(Path(device_object.device_config.app_build_dir) / "zephyr" / ".config")
    with LocalCoapServer(
        address=config.read("CONFIG_TEST_COAP_SERVER"),
        port=config.read_int("CONFIG_TEST_COAP_SERVER_PORT"),
        resources=RESOURCES,
    ) as server:
        yield server
