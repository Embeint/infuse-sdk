# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Local relay in front of the Infuse-IoT cloud ePacket UDP server.

Applications are built with a cloud configuration whose endpoint resolves to the
relay. Datagrams are forwarded unmodified in both directions, so encryption and
acknowledgements are handled by the real cloud. Tests can make the cloud appear
unresponsive by setting `unresponsive`, in which case datagrams in both directions
are discarded.
"""

import logging
import select
import socket
import threading

logger = logging.getLogger(__name__)


class CloudRelay:
    def __init__(self, address: str, port: int, upstream: tuple[str, int]):
        self.address = address
        self.port = port
        self.upstream = upstream
        # Discard all traffic while set
        self.unresponsive = False
        self.forwarded_up = 0
        self.forwarded_down = 0
        self.dropped = 0
        self._socket: socket.socket | None = None
        # Device address -> socket connected to the upstream server
        self._upstream: dict[tuple[str, int], socket.socket] = {}
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        # Resolve once, so later host DNS failures can't interfere with the test
        self.upstream = socket.getaddrinfo(*self.upstream, socket.AF_INET, socket.SOCK_DGRAM)[0][4]
        # No SO_REUSEADDR: a concurrent run on the same port must fail loudly
        # instead of silently sharing the traffic.
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((self.address, self.port))
        self._thread = threading.Thread(target=self._run, name="cloud-relay", daemon=True)
        self._thread.start()
        logger.info("Cloud relay on %s:%d -> %s:%d", self.address, self.port, *self.upstream)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        for sock in self._upstream.values():
            sock.close()
        if self._socket:
            self._socket.close()

    def __enter__(self) -> "CloudRelay":
        self.start()
        return self

    def __exit__(self, *_) -> None:
        self.stop()

    def _upstream_socket(self, peer: tuple[str, int]) -> socket.socket:
        if (sock := self._upstream.get(peer)) is None:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect(self.upstream)
            self._upstream[peer] = sock
        return sock

    def _run(self) -> None:
        while not self._stop.is_set():
            sockets = [self._socket, *self._upstream.values()]
            readable, _, _ = select.select(sockets, [], [], 0.1)
            for sock in readable:
                if sock is self._socket:
                    data, peer = self._socket.recvfrom(4096)
                    if self.unresponsive:
                        self.dropped += 1
                        continue
                    self._upstream_socket(peer).send(data)
                    self.forwarded_up += 1
                else:
                    try:
                        data = sock.recv(4096)
                    except ConnectionRefusedError:
                        continue
                    if self.unresponsive:
                        self.dropped += 1
                        continue
                    peer = next(p for p, s in self._upstream.items() if s is sock)
                    self._socket.sendto(data, peer)
                    self.forwarded_down += 1
