# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psa)

if(CONFIG_BUILD_WITH_TFM)
  # Enable ChaCha20-Poly1305 in the secure image (normally done by CONFIG_INFUSE_SDK)
  set_property(TARGET zephyr_property_target
    APPEND PROPERTY TFM_CMAKE_OPTIONS
    -DTFM_MBEDCRYPTO_PLATFORM_EXTRA_CONFIG_PATH=${INFUSE_DIR}/include/infuse/tfm/mbedtls_config_extra.h
  )
endif()

target_sources(app PRIVATE
  src/main.c
  src/vectors_chachapoly.c
  src/vectors_mac.c
  src/vectors_sha.c
)
