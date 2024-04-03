# Reference, highly optimized, masked C and ASM implementations of Ascon

Ascon is a family of lightweight cryptographic algorithms and consists of:
- Authenticated encryption schemes with associated data (AEAD)
- Hash functions (HASH) and extendible output functions (XOF)
- Pseudo-random functions (PRF) and message authentication codes (MAC)

All implementations use the "ECRYPT Benchmarking of Cryptographic Systems (eBACS)" interface:

- https://bench.cr.yp.to/call-aead.html for AEAD (Ascon-128, Ascon-128a, Ascon-80pq)
- https://bench.cr.yp.to/call-hash.html for HASH and XOF (Ascon-Hash, Ascon-Hasha, Ascon-Xof, Ascon-Xofa)
- https://nacl.cr.yp.to/auth.html for PRF and MAC (Ascon-Mac, Ascon-Prf, Ascon-PrfShort)

For more information on Ascon visit: https://ascon.iaik.tugraz.at/

## Imported

Imported from https://github.com/ascon/ascon-c at commit hash `f1601cb5ff52e65baa475fcc6959e7d6e0be8d77` on 03/04/2024. Only used backends are currently imported.

## Changes

1. AEAD encrypt and decrypt functions build with explicit algorithm names
2. AEAD encrypt and decrypt functions take explicit tag bytes location (Used variants only)
3. Custom build system integration
