# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging
import re
from collections.abc import Iterator

from infuse_pytest.application import expect, run_application
from infuse_pytest.sntp_server import LocalSntpServer
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def expect_sync(lines: Iterator[str]) -> None:
    expect(lines, "Sending request...")
    expect(lines, r"Unix time: \d+")
    expect(lines, r"Now: .* UTC")
    expect(lines, r"Ref: .* UTC")


def test_auto_sntp_triggered(sntp_server: LocalSntpServer, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut)
    assert rc == 0, f"Application exited with {rc}"

    lines = iter(output)

    # Repeated sync point only triggers a single query
    expect(lines, "SNTP query already running")

    # Sync against the default server
    # Sync DNS logs "<host> -> <addr>", async DNS logs "<id> -> <addr>"
    expect(lines, rf" -> {re.escape(sntp_server.responsive_addr)}\b")
    expect_sync(lines)

    # Sync point after the resync age, using the cached address
    expect(lines, "Using cached SNTP address")
    expect_sync(lines)

    # Unresponsive server: query, retry, give up
    expect(lines, rf" -> {re.escape(sntp_server.unresponsive_addr)}\b")
    expect(lines, "SNTP query timeout")
    expect(lines, rf" -> {re.escape(sntp_server.unresponsive_addr)}\b")
    expect(lines, "SNTP query timeout")
    expect(lines, r"Giving up SNTP queries after \d failures")

    # Next sync point after giving up queries the restored server
    expect(lines, rf" -> {re.escape(sntp_server.responsive_addr)}\b")
    expect_sync(lines)

    expect(lines, r"TESTSUITE auto_sntp succeeded")

    # Sync points before the interface was up or before the resync age did not query the server
    logger.info("SNTP queries: %d answered, %d ignored", sntp_server.answered, sntp_server.ignored)
    assert sntp_server.answered == 3
    assert sntp_server.ignored == 2
