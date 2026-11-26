# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Control channel for test applications to change shim behaviour at runtime.

The application sends a single datagram of the form `<command> <argument>`, which
is dispatched to the registered handler. The server responds with `OK` once the
handler has run, or `ERR` if the command is unknown or the handler failed, so the
application can synchronise with the change before continuing.
"""

import logging
from collections.abc import Callable

from .udp_server import UdpServer

logger = logging.getLogger(__name__)


class ShimControl(UdpServer):
    def __init__(self, address: str, port: int, commands: dict[str, Callable[[str], None]]):
        super().__init__(address, port, name="shim-control")
        self.commands = commands
        self.history: list[str] = []

    def handle(self, data: bytes, peer: tuple[str, int]) -> None:
        line = data.decode(errors="replace").strip()
        self.history.append(line)
        command, _, argument = line.partition(" ")
        handler = self.commands.get(command)
        if handler is None:
            logger.warning("Unknown shim command '%s'", line)
            self.send(peer, b"ERR")
            return
        try:
            handler(argument)
        except Exception:
            logger.exception("Shim command '%s' failed", line)
            self.send(peer, b"ERR")
            return
        logger.info("Shim command '%s'", line)
        self.send(peer, b"OK")
