# Copyright 2026 Embeint Holdings Pty Ltd
#
# SPDX-License-Identifier: FSL-1.1-ALv2

"""Local CoAP (RFC 7252) file server, over plain UDP or DTLS 1.2 PSK.

Serves static resources to GET requests with Block2 (RFC 7959) transfers.
Responses are delayed by a fixed latency to emulate a remote server, so that
client timeout behaviour can be exercised over loopback.
"""

import hashlib
import hmac
import logging
import struct
from dataclasses import dataclass, field, replace
from pathlib import Path

from .dtls_psk_server import DtlsPskServer
from .udp_server import PlainUdpServer, UdpServer

logger = logging.getLogger(__name__)

COAP_VERSION = 1
TYPE_CON = 0
TYPE_ACK = 2
TYPE_RST = 3
CODE_EMPTY = 0x00
CODE_GET = 0x01
CODE_CONTENT = 0x45  # 2.05
CODE_BAD_REQUEST = 0x80  # 4.00
CODE_UNAUTHORIZED = 0x81  # 4.01
CODE_BAD_OPTION = 0x82  # 4.02
CODE_NOT_FOUND = 0x84  # 4.04
CODE_METHOD_NOT_ALLOWED = 0x85  # 4.05

OPTION_URI_PATH = 11
OPTION_BLOCK2 = 23
OPTION_SIZE2 = 28

DEFAULT_SZX = 6  # 1024 byte blocks


def hkdf_sha256(secret: bytes, salt: bytes, info: bytes, length: int) -> bytes:
    """HKDF (RFC 5869) with SHA-256"""
    prk = hmac.new(salt, secret, hashlib.sha256).digest()
    output = b""
    block = b""
    counter = 1
    while len(output) < length:
        block = hmac.new(prk, block + info + bytes([counter]), hashlib.sha256).digest()
        output += block
        counter += 1
    return output[:length]


# Credentials used by `CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS`
INFUSE_TEST_DEVICE_ID = 0xFFFFFFFFFFFFFFFD
INFUSE_TEST_SHARED_SECRET = bytes([0xFD] + [0xFF] * 31)


INFUSE_SDK = Path(__file__).resolve().parents[3]


def infuse_test_resources() -> dict[str, bytes]:
    """Resources of the Infuse-IoT test server (`coap.dev.infuse-iot.com`) used by tests.

    `file/med_file` was captured from the server, the DFU images are the cpatch test fixtures.
    """
    cpatch = INFUSE_SDK / "tests" / "subsys" / "cpatch"
    return {
        "file/small_file": b"hello_world\n",
        "file/med_file": (Path(__file__).parent / "resources" / "med_file.bin").read_bytes(),
        "file/hello_world": (cpatch / "bin" / "hello_world.bin").read_bytes(),
        "file/hello_world-validate": (cpatch / "patch" / "hello_world-validate.bin").read_bytes(),
    }


def infuse_test_coap_psk() -> tuple[bytes, bytes]:
    """DTLS PSK identity and key derived by `coap_dtls_load` for the test credentials"""
    identity = f"{INFUSE_TEST_DEVICE_ID:016x}".encode()
    psk = hkdf_sha256(INFUSE_TEST_SHARED_SECRET, struct.pack("<H", 0x7856), b"coap", 32)
    return identity, psk


@dataclass
class CoapRequest:
    msg_type: int
    code: int
    msg_id: int
    token: bytes
    options: list[tuple[int, bytes]]

    def option_values(self, number: int) -> list[bytes]:
        return [v for n, v in self.options if n == number]


def decode_message(data: bytes) -> CoapRequest:
    if len(data) < 4:
        raise ValueError("Message too short")
    ver_type_tkl, code, msg_id = struct.unpack_from("!BBH", data)
    if ver_type_tkl >> 6 != COAP_VERSION:
        raise ValueError("Unsupported version")
    tkl = ver_type_tkl & 0x0F
    token = data[4 : 4 + tkl]
    offset = 4 + tkl
    options = []
    number = 0
    while offset < len(data) and data[offset] != 0xFF:
        delta, length = data[offset] >> 4, data[offset] & 0x0F
        offset += 1
        values = []
        for nibble in (delta, length):
            if nibble == 13:
                values.append(data[offset] + 13)
                offset += 1
            elif nibble == 14:
                values.append(int.from_bytes(data[offset : offset + 2], "big") + 269)
                offset += 2
            elif nibble == 15:
                raise ValueError("Reserved option nibble")
            else:
                values.append(nibble)
        number += values[0]
        options.append((number, data[offset : offset + values[1]]))
        offset += values[1]
    return CoapRequest((ver_type_tkl >> 4) & 0x3, code, msg_id, token, options)


def _uint_bytes(value: int) -> bytes:
    return value.to_bytes((value.bit_length() + 7) // 8, "big")


def _option_nibble(value: int) -> tuple[int, bytes]:
    if value < 13:
        return value, b""
    if value < 269:
        return 13, bytes([value - 13])
    return 14, struct.pack("!H", value - 269)


def encode_message(
    msg_type: int, code: int, msg_id: int, token: bytes, options: list[tuple[int, bytes]], payload: bytes = b""
) -> bytes:
    out = struct.pack("!BBH", (COAP_VERSION << 6) | (msg_type << 4) | len(token), code, msg_id) + token
    previous = 0
    for number, value in sorted(options, key=lambda o: o[0]):
        delta_nibble, delta_ext = _option_nibble(number - previous)
        length_nibble, length_ext = _option_nibble(len(value))
        out += bytes([(delta_nibble << 4) | length_nibble]) + delta_ext + length_ext + value
        previous = number
    if payload:
        out += b"\xff" + payload
    return out


@dataclass
class CoapResource:
    """Resource with a non-default response"""

    payload: bytes = b""
    code: int = CODE_CONTENT
    # Respond with an empty ACK, followed by a separate response after this many seconds
    separate: float | None = None
    # Misbehaviour for client robustness tests:
    # Largest block size (SZX) the server uses, smaller requests are reduced (RFC 7959, section 2.4)
    max_szx: int | None = None
    # Include the Size2 option in block responses
    size2: bool = True
    # Respond with the block this many blocks after the requested block
    block_offset: int = 0
    # Extra bytes appended to each block payload, beyond the block size
    extra_payload: int = 0
    # Respond with the request token truncated or padded to this length
    token_len: int | None = None
    # Send a datagram too short to be a CoAP message before each response
    junk_first: bool = False
    # Reject requests with a Reset message
    reset: bool = False


@dataclass
class LocalCoapServer:
    """CoAP file server, answering each request after `latency` seconds.

    Resources are either the content of the resource, or a `CoapResource` for other
    response codes or separate responses. With `dtls`, the server only accepts DTLS
    connections using the `CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS` PSK.
    """

    address: str
    port: int
    resources: dict[str, bytes | CoapResource]
    dtls: bool = False
    latency: float = 0.025
    requests: list[tuple[str, int, int]] = field(default_factory=list)
    # Message IDs of separate (confirmable) responses sent, and of empty ACKs received
    separate_ids: list[int] = field(default_factory=list)
    acks: list[int] = field(default_factory=list)
    _transport: UdpServer | None = None
    _next_msg_id: int = 0x4000

    @property
    def handshakes(self) -> int:
        return self._transport.handshakes

    @property
    def dtls_failures(self) -> list[str]:
        return self._transport.failures

    def start(self) -> None:
        if self.dtls:
            identity, psk = infuse_test_coap_psk()
            self._transport = DtlsPskServer(
                address=self.address,
                port=self.port,
                psk_lookup=lambda i: psk if i == identity else None,
                on_data=self._handle,
            )
        else:
            self._transport = PlainUdpServer(address=self.address, port=self.port, on_data=self._handle)
        self._transport.start()

    def stop(self) -> None:
        if self._transport:
            self._transport.stop()

    def __enter__(self) -> "LocalCoapServer":
        self.start()
        return self

    def __exit__(self, *_) -> None:
        self.stop()

    def _reply(self, peer, req: CoapRequest, code: int, options=(), payload: bytes = b"") -> None:
        msg = encode_message(TYPE_ACK, code, req.msg_id, req.token, list(options), payload)
        peer.send(msg, delay=self.latency)

    def _separate(self, peer, req: CoapRequest, delay: float, code: int, options=(), payload: bytes = b"") -> None:
        # Empty ACK now, response as a new confirmable message later (RFC 7252, section 5.2.2)
        peer.send(encode_message(TYPE_ACK, CODE_EMPTY, req.msg_id, b"", []), delay=self.latency)
        self._next_msg_id = (self._next_msg_id + 1) & 0xFFFF
        msg = encode_message(TYPE_CON, code, self._next_msg_id, req.token, list(options), payload)
        self.separate_ids.append(self._next_msg_id)
        peer.send(msg, delay=self.latency + delay)

    def _handle(self, peer, data: bytes) -> None:
        try:
            req = decode_message(data)
        except (ValueError, IndexError, struct.error) as e:
            logger.warning("%s: malformed CoAP message: %s", peer.peer, e)
            return
        if req.msg_type != TYPE_CON:
            # ACKs to separate responses
            if req.msg_type == TYPE_ACK and req.code == CODE_EMPTY:
                self.acks.append(req.msg_id)
            return
        if req.code != CODE_GET:
            self._reply(peer, req, CODE_METHOD_NOT_ALLOWED)
            return

        path = "/".join(v.decode(errors="replace") for v in req.option_values(OPTION_URI_PATH))
        block2 = req.option_values(OPTION_BLOCK2)
        block_value = int.from_bytes(block2[0], "big") if block2 else DEFAULT_SZX
        num, szx = block_value >> 4, block_value & 0x7
        if szx == 7:
            self._reply(peer, req, CODE_BAD_OPTION)
            return
        self.requests.append((path, num, 1 << (szx + 4)))

        resource = self.resources.get(path)
        if resource is None:
            resource = CoapResource(code=CODE_NOT_FOUND)
        elif isinstance(resource, bytes):
            resource = CoapResource(payload=resource)

        if resource.reset:
            peer.send(encode_message(TYPE_RST, CODE_EMPTY, req.msg_id, b"", []), delay=self.latency)
            return
        if resource.token_len is not None:
            req = replace(req, token=(req.token + bytes(8))[: resource.token_len])
        if resource.junk_first:
            peer.send(b"\x40\x01", delay=self.latency)

        if resource.code != CODE_CONTENT:
            logger.info("%s: GET %s -> %d.%02d", peer.peer, path, resource.code >> 5, resource.code & 0x1F)
            options, payload = (), b""
        else:
            if resource.max_szx is not None and szx > resource.max_szx:
                num <<= szx - resource.max_szx
                szx = resource.max_szx
            num += resource.block_offset
            size = 1 << (szx + 4)
            start = num * size
            if start >= len(resource.payload) and not (start == 0 and len(resource.payload) == 0):
                self._reply(peer, req, CODE_BAD_OPTION)
                return
            payload = resource.payload[start : start + size] + bytes(resource.extra_payload)
            more = start + size < len(resource.payload)
            logger.debug("%s: GET %s block %d/%d more=%d", peer.peer, path, num, size, more)
            options = [(OPTION_BLOCK2, _uint_bytes((num << 4) | (int(more) << 3) | szx))]
            if resource.size2:
                options.append((OPTION_SIZE2, _uint_bytes(len(resource.payload))))

        if resource.separate is not None:
            self._separate(peer, req, resource.separate, resource.code, options, payload)
        else:
            self._reply(peer, req, resource.code, options, payload)
