#!/usr/bin/env python3

import argparse
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import x25519


def cloud_keygen() -> x25519.X25519PrivateKey:
    output_file = Path("example.pem")
    if output_file.exists():
        with output_file.open("r") as f:
            private_key = serialization.load_pem_private_key(f.read().encode("utf-8"), password=None)
        print(f"Loaded private key from {output_file}")
    else:
        private_key = x25519.X25519PrivateKey.generate()
        pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
        with output_file.open("w") as f:
            f.write(pem.decode("utf-8"))
        print(f"Wrote private key to {output_file}")
    return private_key


def cloud_public_export(private_key: x25519.X25519PrivateKey):
    public_key = private_key.public_key()
    public_bytes = public_key.public_bytes_raw()

    public_array = ",".join([f"0x{b:02x}" for b in public_bytes])
    print("Cloud public key array:")
    print(public_array)
    print("")


def calculate_shared_secret(private_key: x25519.X25519PrivateKey, args: argparse.Namespace):
    device_public_key = x25519.X25519PublicKey.from_public_bytes(bytes.fromhex(args.key))
    shared_secret = private_key.exchange(device_public_key)
    print("Shared secret:")
    print(f"\t{shared_secret.hex()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser("Demonstrate cloud <-> device ECDH shared key derivation", allow_abbrev=False)
    parser.add_argument("--device", dest="key", type=str, help="Device public key")
    args = parser.parse_args()

    private_key = cloud_keygen()
    cloud_public_export(private_key)

    if args.key is not None:
        calculate_shared_secret(private_key, args)
