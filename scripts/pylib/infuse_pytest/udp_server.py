# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""UDP server base with delayed sends, for emulating remote servers over loopback."""

import heapq
import logging
import select
import socket
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class UdpServer:
    """Receives datagrams on a background thread and passes them to `handle`.

    `handle` runs on the server thread. `send` must only be called from the
    server thread (i.e. from within `handle`).
    """

    def __init__(self, address: str, port: int, name: str = "udp-server"):
        self.address = address
        self.port = port
        self._name = name
        self._socket: socket.socket | None = None
        self._scheduled: list[tuple[float, int, tuple[str, int], bytes]] = []
        self._scheduled_count = 0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        # No SO_REUSEADDR: a concurrent run on the same port must fail loudly
        # instead of silently sharing the traffic.
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((self.address, self.port))
        self._thread = threading.Thread(target=self._run, name=self._name, daemon=True)
        self._thread.start()
        logger.info("%s on %s:%d", self._name, self.address, self.port)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self._socket:
            self._socket.close()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_) -> None:
        self.stop()

    def handle(self, data: bytes, peer: tuple[str, int]) -> None:
        raise NotImplementedError

    def send(self, peer: tuple[str, int], datagram: bytes, delay: float = 0.0) -> None:
        """Send `datagram` to `peer`, optionally after `delay` seconds.

        Delayed datagrams are sent even if the peer has since closed its connection,
        as a real network would.
        """
        if delay <= 0 and not self._scheduled:
            self._socket.sendto(datagram, peer)
            return
        self._scheduled_count += 1
        heapq.heappush(self._scheduled, (time.monotonic() + delay, self._scheduled_count, peer, datagram))

    def _flush_scheduled(self) -> None:
        now = time.monotonic()
        while self._scheduled and self._scheduled[0][0] <= now:
            _, _, peer, datagram = heapq.heappop(self._scheduled)
            self._socket.sendto(datagram, peer)

    def _run(self) -> None:
        while not self._stop.is_set():
            timeout = 0.1
            if self._scheduled:
                timeout = max(0.0, min(timeout, self._scheduled[0][0] - time.monotonic()))
            readable, _, _ = select.select([self._socket], [], [], timeout)
            if readable:
                data, peer = self._socket.recvfrom(4096)
                self.handle(data, peer)
            self._flush_scheduled()


@dataclass
class UdpPeer:
    """Remote endpoint of a plain UDP server"""

    server: UdpServer
    peer: tuple[str, int]

    def send(self, data: bytes, delay: float = 0.0) -> None:
        self.server.send(self.peer, data, delay)


class PlainUdpServer(UdpServer):
    """UDP server delivering each datagram to `on_data`"""

    def __init__(self, address: str, port: int, on_data: Callable[[UdpPeer, bytes], None]):
        super().__init__(address, port, name="udp-server")
        self.on_data = on_data

    def handle(self, data: bytes, peer: tuple[str, int]) -> None:
        self.on_data(UdpPeer(self, peer), data)
