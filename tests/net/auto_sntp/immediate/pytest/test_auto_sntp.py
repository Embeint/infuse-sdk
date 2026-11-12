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


def timestamp(match: re.Match) -> float:
    """Device uptime in seconds of a matched log line"""
    m = re.search(r"\[(\d+):(\d{2}):(\d{2})\.(\d{3}),(\d{3})\]", match.string)
    assert m is not None, f"No timestamp in '{match.string}'"
    hours, minutes, seconds, ms, us = (int(g) for g in m.groups())
    return hours * 3600 + minutes * 60 + seconds + ms / 1e3 + us / 1e6


def expect_dns(lines: Iterator[str], address: str) -> None:
    # Sync DNS logs "<host> -> <addr>", async DNS logs "<id> -> <addr>"
    expect(lines, rf" -> {re.escape(address)}\b")


def expect_sync(lines: Iterator[str]) -> None:
    expect(lines, "Sending request...")
    expect(lines, r"Unix time: \d+")


def expect_timeout(lines: Iterator[str]) -> None:
    expect(lines, "Sending request...")
    expect(lines, "SNTP query timeout")


def test_auto_sntp(sntp_server: LocalSntpServer, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut)
    assert rc == 0, f"Application exited with {rc}"

    lines = iter(output)

    # Initial sync against the default server
    expect_dns(lines, sntp_server.responsive_addr)
    expect_sync(lines)

    # Server switched to one that never responds, query is retried after a DNS requery
    expect_dns(lines, sntp_server.unresponsive_addr)
    expect(lines, "Sending request...")
    first_timeout = expect(lines, "SNTP query timeout")
    expect_dns(lines, sntp_server.unresponsive_addr)
    retry = expect(lines, "Sending request...")
    expect(lines, "SNTP query timeout")
    expect(lines, r"Giving up SNTP queries after \d failures")

    # Retry uses CONFIG_SNTP_AUTO_RETRY_PERIOD_MS (500 ms), not the resync age (3 s)
    retry_delay = timestamp(retry) - timestamp(first_timeout)
    logger.info("SNTP retry delay: %.3f s", retry_delay)
    assert retry_delay < 1.5, f"SNTP retry took {retry_delay:.3f} s"

    # Server switched back to the default
    expect_dns(lines, sntp_server.responsive_addr)
    expect_sync(lines)

    # Resync after manual reference, using the cached address
    expect(lines, "Using cached SNTP address")
    expect_sync(lines)

    # Resync after interface reconnection, using the cached address
    expect(lines, "Using cached SNTP address")
    expect_sync(lines)

    expect(lines, r"TESTSUITE auto_sntp succeeded")

    # Every query reached the local server
    logger.info("SNTP queries: %d answered, %d ignored", sntp_server.answered, sntp_server.ignored)
    assert sntp_server.answered == 4
    assert sntp_server.ignored == 2
