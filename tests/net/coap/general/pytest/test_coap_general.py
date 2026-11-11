# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging

from infuse_pytest.application import expect, run_application
from infuse_pytest.coap_server import LocalCoapServer
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def test_coap_general(coap_server: LocalCoapServer, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut)
    assert rc == 0, f"Application exited with {rc}"
    expect(iter(output), r"TESTSUITE infuse_coap succeeded")

    logger.info("CoAP requests: %s", coap_server.requests)
    requested = set(coap_server.requests)

    # Error responses, multi segment paths and the separate response
    for path in ("secret", "invalid-path", "location-query", "hello", "seg1/seg2/seg3", "separate"):
        assert any(p == path for p, _, _ in requested), path

    # Each block request is sent CONFIG_INFUSE_COAP_MAX_TIMEOUTS (3) times before failing
    attempts = [r for r in coap_server.requests if r[0] == "hello/short_token"]
    assert len(attempts) == 3, f"Unexpected attempts: {attempts}"

    # Every block of the large resource was requested at each block size used
    for block_size in (1024, 512, 256):
        blocks = -(-1700 // block_size)
        missing = [n for n in range(blocks) if ("large", n, block_size) not in requested]
        assert missing == [], f"{block_size} byte blocks not requested: {missing}"
