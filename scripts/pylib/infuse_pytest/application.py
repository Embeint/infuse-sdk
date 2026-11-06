# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Run simulated applications to completion and validate their output."""

import logging
import re
import subprocess
from collections.abc import Iterator

from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def run_application(device: DeviceAdapter, timeout: float = 120.0) -> tuple[int, list[str]]:
    """Run the application to completion, returning the exit code and all output.

    The harness only reads output while the process is running, so output produced
    immediately before the process exits (or all output of a short lived process) is
    not reliably observable through it. Running the application directly guarantees
    the complete output is observed.

    :param device: Device from the `unlaunched_dut` fixture
    :param timeout: Maximum duration the application can run for
    """
    device.generate_command()
    if device.device_config.extra_test_args:
        device.command.extend(device.device_config.extra_test_args.split())
    result = subprocess.run(
        device.command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        env=device.process_kwargs["env"],
        cwd=device.process_kwargs["cwd"],
        timeout=timeout,
        check=False,
    )
    lines = result.stdout.decode(errors="replace").splitlines()
    for line in lines:
        logger.debug("#: %s", line)
    return result.returncode, lines


def expect(lines: Iterator[str], pattern: str) -> re.Match:
    """Consume output lines until `pattern` matches"""
    regex = re.compile(pattern)
    for line in lines:
        if m := regex.search(line):
            return m
    raise AssertionError(f"'{pattern}' not observed")
