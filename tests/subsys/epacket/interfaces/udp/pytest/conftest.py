# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

import sys
from collections.abc import Generator
from pathlib import Path

import pytest
from twister_harness import DeviceAdapter

sys.path.insert(0, str(Path(__file__).resolve().parents[6] / "scripts" / "pylib"))

from infuse_pytest.cloud_shims import CloudShims, cloud_shims  # noqa: E402


@pytest.fixture(scope="session")
def shims(device_object: DeviceAdapter) -> Generator[CloudShims, None, None]:
    """Cloud relay, DNS server and control channel for the application.

    Session scoped so that they are running before the application is launched.
    """
    build_dir = Path(device_object.device_config.app_build_dir)
    with cloud_shims(build_dir, Path(__file__).resolve().parents[1]) as s:
        yield s
