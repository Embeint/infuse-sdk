# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Local SNTP server.

Answers queries on the responsive address with the host time. Queries to the
optional unresponsive address are received but never answered, exercising
client timeout paths.
"""

import logging
import select
import socket
import struct
import threading
import time
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)

# Seconds between the NTP epoch (1900) and the Unix epoch (1970)
NTP_UNIX_OFFSET = 2208988800
NTP_PACKET = struct.Struct("!BBbbIII8s8s8s8s")
NTP_MODE_CLIENT = 3
NTP_MODE_SERVER = 4


def ntp_timestamp(unix_time: float) -> bytes:
    seconds = int(unix_time)
    fraction = int((unix_time - seconds) * (1 << 32))
    return struct.pack("!II", (seconds + NTP_UNIX_OFFSET) & 0xFFFFFFFF, fraction)


@dataclass
class LocalSntpServer:
    port: int
    responsive_addr: str = "127.0.0.1"
    unresponsive_addr: str | None = None
    answered: int = 0
    ignored: int = 0
    _responsive: socket.socket | None = None
    _unresponsive: socket.socket | None = None
    _stop: threading.Event = field(default_factory=threading.Event)
    _thread: threading.Thread | None = None

    def _bind(self, addr: str) -> socket.socket:
        # No SO_REUSEADDR: a concurrent run on the same port must fail loudly
        # instead of silently sharing the queries.
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((addr, self.port))
        return sock

    def _sockets(self) -> list[socket.socket]:
        return [s for s in (self._responsive, self._unresponsive) if s is not None]

    def start(self) -> None:
        self._responsive = self._bind(self.responsive_addr)
        if self.unresponsive_addr:
            self._unresponsive = self._bind(self.unresponsive_addr)
        self._thread = threading.Thread(target=self._run, name="sntp-server", daemon=True)
        self._thread.start()
        logger.info(
            "SNTP server on %s:%d (unresponsive %s)",
            self.responsive_addr,
            self.port,
            self.unresponsive_addr,
        )

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        for sock in self._sockets():
            sock.close()

    def __enter__(self) -> "LocalSntpServer":
        self.start()
        return self

    def __exit__(self, *_) -> None:
        self.stop()

    def _run(self) -> None:
        sockets = self._sockets()
        while not self._stop.is_set():
            readable, _, _ = select.select(sockets, [], [], 0.1)
            for sock in readable:
                data, peer = sock.recvfrom(512)
                rx_time = time.time()
                if sock is self._unresponsive:
                    self.ignored += 1
                    logger.info("Ignoring SNTP query from %s:%d", *peer)
                    continue
                response = self._build_response(data, rx_time)
                if response is None:
                    logger.warning("Malformed SNTP query from %s:%d", *peer)
                    continue
                sock.sendto(response, peer)
                self.answered += 1
                logger.info("Answered SNTP query from %s:%d", *peer)

    @staticmethod
    def _build_response(data: bytes, rx_time: float) -> bytes | None:
        if len(data) < NTP_PACKET.size:
            return None
        li_vn_mode, _, poll, _, _, _, _, _, _, _, client_tx = NTP_PACKET.unpack_from(data)
        version = (li_vn_mode >> 3) & 0x7
        if li_vn_mode & 0x7 != NTP_MODE_CLIENT:
            return None
        now = ntp_timestamp(time.time())
        return NTP_PACKET.pack(
            (0 << 6) | (version << 3) | NTP_MODE_SERVER,
            1,  # Stratum: primary reference
            poll,
            -20,  # Precision: ~1 us
            0,  # Root delay
            0,  # Root dispersion
            int.from_bytes(b"LOCL", "big"),  # Reference ID
            now,  # Reference timestamp
            client_tx,  # Originate timestamp (echo of client transmit)
            ntp_timestamp(rx_time),  # Receive timestamp
            now,  # Transmit timestamp
        )
