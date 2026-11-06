# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging

from infuse_pytest.application import expect, run_application
from infuse_pytest.coap_server import LocalCoapServer
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def test_coap(coap_server: LocalCoapServer, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut)
    assert rc == 0, f"Application exited with {rc}"
    expect(iter(output), r"TESTSUITE infuse_coap succeeded")

    logger.info("DTLS handshakes: %d", coap_server.handshakes)
    logger.info("CoAP requests: %s", coap_server.requests)
    assert coap_server.dtls_failures == []

    # One DTLS session for each test that opens a socket
    # (timeout, socket_close, download, download_early_terminate)
    assert coap_server.handshakes == 4

    # Every block of the medium file was requested at each block size used
    requested = set(coap_server.requests)
    assert any(path == "file/small_file" for path, _, _ in requested)
    for block_size in (1024, 512, 256):
        blocks = -(-10030 // block_size)
        missing = [n for n in range(blocks) if ("file/med_file", n, block_size) not in requested]
        assert missing == [], f"{block_size} byte blocks not requested: {missing}"
