# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Local DNS server.

Answers A and AAAA queries for any name with fixed documentation addresses,
except names under the reserved `.invalid` TLD, which return NXDOMAIN.
"""

import ipaddress
import logging
import select
import socket
import struct
import threading
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)

DNS_HEADER = struct.Struct("!HHHHHH")
QTYPE_A = 1
QTYPE_AAAA = 28
QCLASS_IN = 1
RCODE_NOERROR = 0
RCODE_FORMERR = 1
RCODE_NXDOMAIN = 3

ANSWER_A = ipaddress.IPv4Address("192.0.2.10")
ANSWER_AAAA = ipaddress.IPv6Address("2001:db8::10")


def parse_question(data: bytes) -> tuple[str, int, int, int]:
    """Parse the first question, returning (name, qtype, qclass, end offset)"""
    labels = []
    offset = DNS_HEADER.size
    while (length := data[offset]) != 0:
        if length & 0xC0:
            raise ValueError("Compressed names not expected in queries")
        labels.append(data[offset + 1 : offset + 1 + length].decode("ascii"))
        offset += 1 + length
    offset += 1
    qtype, qclass = struct.unpack_from("!HH", data, offset)
    return ".".join(labels) + ".", qtype, qclass, offset + 4


@dataclass
class LocalDnsServer:
    address: str
    port: int
    queries: list[tuple[str, str]] = field(default_factory=list)
    _socket: socket.socket | None = None
    _stop: threading.Event = field(default_factory=threading.Event)
    _thread: threading.Thread | None = None

    def start(self) -> None:
        # No SO_REUSEADDR: a concurrent run on the same port must fail loudly
        # instead of silently sharing the queries.
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((self.address, self.port))
        self._thread = threading.Thread(target=self._run, name="dns-server", daemon=True)
        self._thread.start()
        logger.info("DNS server on %s:%d", self.address, self.port)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self._socket:
            self._socket.close()

    def __enter__(self) -> "LocalDnsServer":
        self.start()
        return self

    def __exit__(self, *_) -> None:
        self.stop()

    def _run(self) -> None:
        while not self._stop.is_set():
            readable, _, _ = select.select([self._socket], [], [], 0.1)
            if not readable:
                continue
            data, peer = self._socket.recvfrom(512)
            response = self._build_response(data)
            if response is not None:
                self._socket.sendto(response, peer)

    def _build_response(self, data: bytes) -> bytes | None:
        if len(data) < DNS_HEADER.size:
            return None
        txid, flags, qdcount, _, _, _ = DNS_HEADER.unpack_from(data)
        # Response, copy opcode and RD, authoritative, recursion available
        rsp_flags = 0x8000 | (flags & 0x7900) | 0x0400 | 0x0080

        try:
            if qdcount != 1:
                raise ValueError(f"Unexpected question count {qdcount}")
            name, qtype, qclass, end = parse_question(data)
        except (ValueError, IndexError, UnicodeDecodeError, struct.error) as e:
            logger.warning("Malformed DNS query: %s", e)
            return DNS_HEADER.pack(txid, rsp_flags | RCODE_FORMERR, 0, 0, 0, 0)

        type_str = {QTYPE_A: "A", QTYPE_AAAA: "AAAA"}.get(qtype, str(qtype))
        self.queries.append((name, type_str))
        question = data[DNS_HEADER.size : end]

        if name.lower().endswith(".invalid."):
            logger.info("NXDOMAIN %s %s", type_str, name)
            return DNS_HEADER.pack(txid, rsp_flags | RCODE_NXDOMAIN, 1, 0, 0, 0) + question

        rdata = None
        if qclass == QCLASS_IN and qtype == QTYPE_A:
            rdata = ANSWER_A.packed
        elif qclass == QCLASS_IN and qtype == QTYPE_AAAA:
            rdata = ANSWER_AAAA.packed
        if rdata is None:
            # Name exists, but no records of the requested type
            logger.info("NODATA %s %s", type_str, name)
            return DNS_HEADER.pack(txid, rsp_flags | RCODE_NOERROR, 1, 0, 0, 0) + question

        logger.info("Answer %s %s", type_str, name)
        # Name as a pointer to the question, TTL of 60 seconds
        answer = struct.pack("!HHHIH", 0xC00C, qtype, qclass, 60, len(rdata)) + rdata
        return DNS_HEADER.pack(txid, rsp_flags | RCODE_NOERROR, 1, 1, 0, 0) + question + answer
