# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Minimal DTLS 1.2 PSK server.

Implements just enough of DTLS 1.2 (RFC 6347) to terminate connections from
the Zephyr mbedTLS client using TLS_PSK_WITH_CHACHA20_POLY1305_SHA256
(RFC 7905):
 * No HelloVerifyRequest cookie exchange (optional for the server).
 * No session resumption, renegotiation, fragmentation or retransmission.
 * No extensions other than an empty `renegotiation_info` echo.

This is test infrastructure for loopback use only. It performs no replay
protection and has not been hardened against malicious peers.
"""

import hashlib
import hmac
import logging
import os
import struct
from collections.abc import Callable
from dataclasses import dataclass, field

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

from .udp_server import UdpServer

logger = logging.getLogger(__name__)

DTLS_1_2 = 0xFEFD
RECORD_HEADER = struct.Struct("!BHHIHH")  # type, version, epoch, seq (48 bit as 32 + 16), length
HANDSHAKE_HEADER_LEN = 12

CT_CHANGE_CIPHER_SPEC = 20
CT_ALERT = 21
CT_HANDSHAKE = 22
CT_APPLICATION_DATA = 23

HS_CLIENT_HELLO = 1
HS_SERVER_HELLO = 2
HS_SERVER_HELLO_DONE = 14
HS_CLIENT_KEY_EXCHANGE = 16
HS_FINISHED = 20

TLS_PSK_WITH_CHACHA20_POLY1305_SHA256 = 0xCCAB
TLS_EMPTY_RENEGOTIATION_INFO_SCSV = 0x00FF
EXT_RENEGOTIATION_INFO = 0xFF01

ALERT_FATAL = 2
ALERT_CLOSE_NOTIFY = 0
ALERT_HANDSHAKE_FAILURE = 40
ALERT_DECRYPT_ERROR = 51
ALERT_UNKNOWN_PSK_IDENTITY = 115


class DtlsError(Exception):
    def __init__(self, alert: int, msg: str):
        super().__init__(msg)
        self.alert = alert


def prf_sha256(secret: bytes, label: bytes, seed: bytes, length: int) -> bytes:
    """TLS 1.2 PRF (RFC 5246, section 5)"""
    seed = label + seed
    output = b""
    a = seed
    while len(output) < length:
        a = hmac.new(secret, a, hashlib.sha256).digest()
        output += hmac.new(secret, a + seed, hashlib.sha256).digest()
    return output[:length]


def _pack_seq(seq: int) -> tuple[int, int]:
    return seq >> 16, seq & 0xFFFF


def _vector(data: bytes, offset: int, len_bytes: int) -> tuple[bytes, int]:
    length = int.from_bytes(data[offset : offset + len_bytes], "big")
    start = offset + len_bytes
    if start + length > len(data):
        raise DtlsError(ALERT_HANDSHAKE_FAILURE, "Truncated vector")
    return data[start : start + length], start + length


@dataclass
class DtlsSession:
    """State of a single DTLS connection"""

    server: "DtlsPskServer"
    peer: tuple[str, int]
    identity: bytes = b""
    established: bool = False
    _client_random: bytes = b""
    _server_random: bytes = b""
    _transcript: bytes = b""
    _master_secret: bytes = b""
    _next_msg_seq: int = 0
    _write_epoch: int = 0
    _write_seq: dict[int, int] = field(default_factory=lambda: {0: 0, 1: 0})
    _read_epoch: int = 0
    _client_aead: ChaCha20Poly1305 | None = None
    _client_iv: bytes = b""
    _server_aead: ChaCha20Poly1305 | None = None
    _server_iv: bytes = b""

    def send(self, data: bytes, delay: float = 0.0) -> None:
        """Send application data to the peer, optionally after a delay"""
        self.server.send(self.peer, self._record(CT_APPLICATION_DATA, data), delay)

    # Record layer

    def _nonce_aad(self, iv: bytes, epoch: int, seq: int, ctype: int, length: int) -> tuple[bytes, bytes]:
        seq64 = (epoch << 48) | seq
        nonce = bytes(a ^ b for a, b in zip(iv, b"\x00" * 4 + seq64.to_bytes(8, "big"), strict=True))
        aad = seq64.to_bytes(8, "big") + struct.pack("!BHH", ctype, DTLS_1_2, length)
        return nonce, aad

    def _record(self, ctype: int, payload: bytes) -> bytes:
        epoch = self._write_epoch
        seq = self._write_seq[epoch]
        self._write_seq[epoch] += 1
        if epoch > 0:
            nonce, aad = self._nonce_aad(self._server_iv, epoch, seq, ctype, len(payload))
            payload = self._server_aead.encrypt(nonce, payload, aad)
        seq_hi, seq_lo = _pack_seq(seq)
        return RECORD_HEADER.pack(ctype, DTLS_1_2, epoch, seq_hi, seq_lo, len(payload)) + payload

    def _handshake(self, msg_type: int, body: bytes) -> bytes:
        seq = self._next_msg_seq
        self._next_msg_seq += 1
        length = len(body).to_bytes(3, "big")
        msg = struct.pack("!B3sH3s3s", msg_type, length, seq, b"\x00" * 3, length) + body
        self._transcript += msg
        return self._record(CT_HANDSHAKE, msg)

    def handle_datagram(self, data: bytes) -> bytes:
        """Process all records in a datagram, returning any response datagram"""
        response = b""
        offset = 0
        while offset + RECORD_HEADER.size <= len(data):
            ctype, _, epoch, seq_hi, seq_lo, length = RECORD_HEADER.unpack_from(data, offset)
            offset += RECORD_HEADER.size
            fragment = data[offset : offset + length]
            offset += length
            if epoch != self._read_epoch:
                logger.debug("%s: dropping record from epoch %d", self.peer, epoch)
                continue
            if epoch > 0:
                seq = (seq_hi << 16) | seq_lo
                nonce, aad = self._nonce_aad(self._client_iv, epoch, seq, ctype, length - 16)
                try:
                    fragment = self._client_aead.decrypt(nonce, fragment, aad)
                except InvalidTag as e:
                    raise DtlsError(ALERT_DECRYPT_ERROR, "Record authentication failed") from e
            response += self._handle_record(ctype, fragment)
        return response

    def _handle_record(self, ctype: int, payload: bytes) -> bytes:
        if ctype == CT_HANDSHAKE:
            response = b""
            offset = 0
            while offset + HANDSHAKE_HEADER_LEN <= len(payload):
                msg_type = payload[offset]
                length = int.from_bytes(payload[offset + 1 : offset + 4], "big")
                frag_off = int.from_bytes(payload[offset + 6 : offset + 9], "big")
                frag_len = int.from_bytes(payload[offset + 9 : offset + 12], "big")
                if frag_off != 0 or frag_len != length:
                    raise DtlsError(ALERT_HANDSHAKE_FAILURE, "Fragmented handshake not supported")
                msg = payload[offset : offset + HANDSHAKE_HEADER_LEN + length]
                offset += HANDSHAKE_HEADER_LEN + length
                response += self._handle_handshake(msg_type, msg)
            return response
        if ctype == CT_CHANGE_CIPHER_SPEC:
            self._read_epoch = 1
            return b""
        if ctype == CT_ALERT:
            level, desc = payload[0], payload[1]
            logger.info("%s: alert level %d description %d", self.peer, level, desc)
            self.server.close_session(self.peer)
            return b""
        if ctype == CT_APPLICATION_DATA:
            if not self.established:
                raise DtlsError(ALERT_HANDSHAKE_FAILURE, "Application data before handshake")
            self.server.on_data(self, payload)
            return b""
        logger.warning("%s: unknown content type %d", self.peer, ctype)
        return b""

    # Handshake

    def _handle_handshake(self, msg_type: int, msg: bytes) -> bytes:
        body = msg[HANDSHAKE_HEADER_LEN:]
        if msg_type == HS_CLIENT_HELLO:
            return self._client_hello(msg, body)
        if msg_type == HS_CLIENT_KEY_EXCHANGE:
            self._transcript += msg
            self._client_key_exchange(body)
            return b""
        if msg_type == HS_FINISHED:
            return self._client_finished(msg, body)
        raise DtlsError(ALERT_HANDSHAKE_FAILURE, f"Unexpected handshake message {msg_type}")

    def _client_hello(self, msg: bytes, body: bytes) -> bytes:
        version = int.from_bytes(body[0:2], "big")
        if version != DTLS_1_2:
            raise DtlsError(ALERT_HANDSHAKE_FAILURE, f"Unsupported version {version:04x}")
        self._client_random = body[2:34]
        offset = 34
        _session_id, offset = _vector(body, offset, 1)
        _cookie, offset = _vector(body, offset, 1)
        suites_raw, offset = _vector(body, offset, 2)
        _compression, offset = _vector(body, offset, 1)
        extensions = []
        if offset < len(body):
            ext_raw, offset = _vector(body, offset, 2)
            ext_offset = 0
            while ext_offset + 4 <= len(ext_raw):
                ext_type = int.from_bytes(ext_raw[ext_offset : ext_offset + 2], "big")
                _, ext_offset = _vector(ext_raw, ext_offset + 2, 2)
                extensions.append(ext_type)
        suites = [int.from_bytes(suites_raw[i : i + 2], "big") for i in range(0, len(suites_raw), 2)]
        logger.debug(
            "%s: ClientHello suites %s extensions %s",
            self.peer,
            [f"{s:04x}" for s in suites],
            [f"{e:04x}" for e in extensions],
        )
        if TLS_PSK_WITH_CHACHA20_POLY1305_SHA256 not in suites:
            raise DtlsError(ALERT_HANDSHAKE_FAILURE, "TLS_PSK_WITH_CHACHA20_POLY1305_SHA256 not offered")

        self._transcript += msg
        # Server message sequence numbers mirror the client (no HelloVerifyRequest)
        self._next_msg_seq = int.from_bytes(msg[4:6], "big")
        self._server_random = os.urandom(32)

        server_extensions = b""
        if EXT_RENEGOTIATION_INFO in extensions or TLS_EMPTY_RENEGOTIATION_INFO_SCSV in suites:
            server_extensions += struct.pack("!HHB", EXT_RENEGOTIATION_INFO, 1, 0)
        server_hello = (
            struct.pack("!H", DTLS_1_2)
            + self._server_random
            + b"\x00"  # Empty session ID, no resumption
            + struct.pack("!HB", TLS_PSK_WITH_CHACHA20_POLY1305_SHA256, 0)
        )
        if server_extensions:
            server_hello += struct.pack("!H", len(server_extensions)) + server_extensions
        return self._handshake(HS_SERVER_HELLO, server_hello) + self._handshake(HS_SERVER_HELLO_DONE, b"")

    def _client_key_exchange(self, body: bytes) -> None:
        self.identity, _ = _vector(body, 0, 2)
        psk = self.server.psk_lookup(self.identity)
        if psk is None:
            raise DtlsError(ALERT_UNKNOWN_PSK_IDENTITY, f"Unknown PSK identity {self.identity!r}")

        # RFC 4279, section 2
        premaster = struct.pack("!H", len(psk)) + b"\x00" * len(psk) + struct.pack("!H", len(psk)) + psk
        self._master_secret = prf_sha256(premaster, b"master secret", self._client_random + self._server_random, 48)
        key_block = prf_sha256(self._master_secret, b"key expansion", self._server_random + self._client_random, 88)
        self._client_aead = ChaCha20Poly1305(key_block[0:32])
        self._server_aead = ChaCha20Poly1305(key_block[32:64])
        self._client_iv = key_block[64:76]
        self._server_iv = key_block[76:88]

    def _verify_data(self, label: bytes) -> bytes:
        return prf_sha256(self._master_secret, label, hashlib.sha256(self._transcript).digest(), 12)

    def _client_finished(self, msg: bytes, body: bytes) -> bytes:
        if self._read_epoch != 1:
            raise DtlsError(ALERT_HANDSHAKE_FAILURE, "Finished before ChangeCipherSpec")
        if not hmac.compare_digest(body, self._verify_data(b"client finished")):
            raise DtlsError(ALERT_DECRYPT_ERROR, "Client Finished verification failed")
        self._transcript += msg

        response = self._record(CT_CHANGE_CIPHER_SPEC, b"\x01")
        self._write_epoch = 1
        response += self._handshake(HS_FINISHED, self._verify_data(b"server finished"))
        self.established = True
        self.server.handshakes += 1
        logger.info("%s: DTLS session established (identity %s)", self.peer, self.identity.decode(errors="replace"))
        return response


class DtlsPskServer(UdpServer):
    """DTLS 1.2 PSK server, delivering decrypted application data to `on_data`"""

    def __init__(
        self,
        address: str,
        port: int,
        psk_lookup: Callable[[bytes], bytes | None],
        on_data: Callable[[DtlsSession, bytes], None],
    ):
        super().__init__(address, port, name="dtls-server")
        self.psk_lookup = psk_lookup
        self.on_data = on_data
        self.handshakes = 0
        self.failures: list[str] = []
        self._sessions: dict[tuple[str, int], DtlsSession] = {}

    def close_session(self, peer: tuple[str, int]) -> None:
        self._sessions.pop(peer, None)

    def handle(self, data: bytes, peer: tuple[str, int]) -> None:
        session = self._sessions.get(peer)
        if session is None or (data[0] == CT_HANDSHAKE and data[3:5] == b"\x00\x00" and session.established):
            # New connection (or a new handshake from the same port)
            session = DtlsSession(server=self, peer=peer)
            self._sessions[peer] = session
        try:
            response = session.handle_datagram(data)
        except DtlsError as e:
            logger.error("%s: %s", peer, e)
            self.failures.append(str(e))
            self.send(peer, session._record(CT_ALERT, bytes([ALERT_FATAL, e.alert])))
            self.close_session(peer)
            return
        if response:
            self.send(peer, response)
