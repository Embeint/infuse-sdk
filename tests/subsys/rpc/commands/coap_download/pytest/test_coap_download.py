# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging

from infuse_pytest.application import expect, run_application
from infuse_pytest.coap_server import LocalCoapServer
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def test_coap_download(coap_server: LocalCoapServer, unresponsive_server: list[bytes], unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut, timeout=300.0)
    assert rc == 0, f"Application exited with {rc}"
    expect(iter(output), r"TESTSUITE rpc_command_coap_download succeeded")

    logger.info("DTLS handshakes: %d", coap_server.handshakes)
    assert coap_server.dtls_failures == []

    # Every resource used by the application was requested, including the missing file
    requested = {path for path, _, _ in coap_server.requests}
    logger.info("CoAP resources requested: %s", sorted(requested))
    for path in ("file/small_file", "file/med_file", "file/hello_world", "file/hello_world-validate"):
        assert path in requested, path
    assert "file/doesn't-exist" in requested

    # The connection failure test attempted a handshake with the unresponsive server
    logger.info("Datagrams to unresponsive server: %d", len(unresponsive_server))
    assert len(unresponsive_server) > 0
