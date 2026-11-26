# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import logging

from infuse_pytest.application import expect, run_application
from infuse_pytest.cloud_shims import CloudShims
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def test_epacket_udp(shims: CloudShims, unlaunched_dut: DeviceAdapter):
    rc, output = run_application(unlaunched_dut, timeout=300.0)
    assert rc == 0, f"Application exited with {rc}"
    expect(iter(output), r"TESTSUITE epacket_udp succeeded")

    logger.info(
        "Relay: %d up, %d down, %d dropped",
        shims.relay.forwarded_up,
        shims.relay.forwarded_down,
        shims.relay.dropped,
    )
    # Traffic reached the cloud through the relay, and responses came back
    assert shims.relay.forwarded_up > 0
    assert shims.relay.forwarded_down > 0
    # Unresponsive cloud and DNS failure behaviour was exercised
    assert shims.relay.dropped > 0
    assert "cloud_drop 1" in shims.control.history
    assert "dns_fail 1" in shims.control.history
    # Cloud URL from the cloud configuration was resolved through the local DNS server
    assert ("cloud.test.", "A") in shims.dns.queries
