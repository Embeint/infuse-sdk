# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Shims standing in for the network path between an application and the cloud.

The application is built with:
  * `CONFIG_INFUSE_SECURITY_CLOUD_PUBLIC_KEY_FILE` pointing at a cloud configuration
    using the default cloud public key, with a `url` that only the local DNS server
    resolves (to `127.0.0.1`), and the relay `port`.
  * `CONFIG_DNS_SERVER1` pointing at the local DNS server.
  * `CONFIG_TEST_SHIM_CONTROL_PORT` for the control channel on `127.0.0.1`.

Supported control commands:
  * `cloud_drop <0|1>`: cloud relay discards all traffic (cloud not responding)
  * `dns_fail <0|1>`: all DNS queries fail
"""

import contextlib
from collections.abc import Generator
from dataclasses import dataclass
from pathlib import Path

import yaml
from twister_harness.helpers.config_reader import ConfigReader

from .cloud_relay import CloudRelay
from .dns_server import LocalDnsServer
from .shim_control import ShimControl

INFUSE_SDK = Path(__file__).resolve().parents[3]
DEFAULT_CLOUD = INFUSE_SDK / "lib" / "security" / "default_cloud_public_key.yaml"
LOCALHOST = "127.0.0.1"


@dataclass
class CloudShims:
    relay: CloudRelay
    dns: LocalDnsServer
    control: ShimControl


def _load_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def _flag(argument: str) -> bool:
    if argument not in ("0", "1"):
        raise ValueError(f"Expected 0 or 1, got '{argument}'")
    return argument == "1"


@contextlib.contextmanager
def cloud_shims(build_dir: Path, test_dir: Path) -> Generator[CloudShims, None, None]:
    """Run the cloud relay, DNS server and control channel for an application

    :param build_dir: Application build directory
    :param test_dir: Application source directory, for resolving the cloud configuration
    """
    config = ConfigReader(build_dir / "zephyr" / ".config")

    cloud_file = Path(config.read("CONFIG_INFUSE_SECURITY_CLOUD_PUBLIC_KEY_FILE"))
    if not cloud_file.is_absolute():
        cloud_file = test_dir / cloud_file
    cloud = _load_yaml(cloud_file)
    upstream = _load_yaml(DEFAULT_CLOUD)
    if cloud["key"] != upstream["key"]:
        raise ValueError(f"{cloud_file} must use the default cloud public key to relay to the cloud")

    dns_address, dns_port = config.read("CONFIG_DNS_SERVER1").rsplit(":", 1)

    relay = CloudRelay(LOCALHOST, cloud["port"], (upstream["url"], upstream["port"]))
    dns = LocalDnsServer(address=dns_address, port=int(dns_port), answers={cloud["url"]: LOCALHOST})
    control = ShimControl(
        LOCALHOST,
        config.read_int("CONFIG_TEST_SHIM_CONTROL_PORT"),
        {
            "cloud_drop": lambda arg: setattr(relay, "unresponsive", _flag(arg)),
            "dns_fail": lambda arg: setattr(dns, "fail", _flag(arg)),
        },
    )
    with relay, dns, control:
        yield CloudShims(relay, dns, control)
