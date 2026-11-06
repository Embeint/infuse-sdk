# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging
import re
from collections.abc import Iterator

from infuse_pytest.application import expect, run_application
from infuse_pytest.dns_server import ANSWER_A, ANSWER_AAAA, LocalDnsServer
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

VALID = "google.com."
INVALID = "does-not-exist.invalid."


def expect_async(lines: Iterator[str], name: str, result: str) -> None:
    """Async query for `name` was started, and the query with that ID produced `result`"""
    m = expect(lines, rf"Started DNS query for '{re.escape(name)}' \(ID ([0-9A-F]{{4}})\)")
    expect(lines, rf"{m.group(1)} -> {result}")


def test_dns(dns_server: LocalDnsServer, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut)
    assert rc == 0, f"Application exited with {rc}"

    lines = iter(output)
    ipv4 = rf"{re.escape(str(ANSWER_A))}\b"
    ipv6 = rf"{re.escape(str(ANSWER_AAAA))}\b"

    # Synchronous queries
    expect(lines, rf"{re.escape(VALID)} -> {ipv4}")
    expect(lines, rf"{re.escape(INVALID)} -> Lookup failed")
    expect(lines, rf"{re.escape(VALID)} -> {ipv6}")
    expect(lines, rf"{re.escape(INVALID)} -> Lookup failed")

    # Asynchronous queries
    expect_async(lines, VALID, ipv4)
    expect_async(lines, INVALID, "DNS resolving error")
    expect_async(lines, VALID, ipv6)
    expect_async(lines, INVALID, "DNS resolving error")

    # Every query reached the local server, in order
    logger.info("DNS queries: %s", dns_server.queries)
    expected = [(VALID, "A"), (INVALID, "A"), (VALID, "AAAA"), (INVALID, "AAAA")]
    assert dns_server.queries == 2 * expected
