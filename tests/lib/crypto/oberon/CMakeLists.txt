# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oberon)

target_sources(app PRIVATE
  src/main.c
)

if(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR)
  # Count calls into the Oberon library to validate PSA dispatch routing
  foreach(fn
      ocrypto_chacha20_poly1305_encrypt
      ocrypto_chacha20_poly1305_decrypt
      ocrypto_sha1 ocrypto_sha1_final
      ocrypto_sha224 ocrypto_sha224_final
      ocrypto_sha256 ocrypto_sha256_final
      ocrypto_sha384 ocrypto_sha384_final
      ocrypto_sha512 ocrypto_sha512_final
      ocrypto_curve25519_scalarmult
    )
    zephyr_link_libraries(-Wl,--wrap=${fn})
  endforeach()
endif()
