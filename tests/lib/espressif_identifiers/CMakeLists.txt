# Copyright (c) 2026 Embeint Holdings Pty Ltd
# SPDX-License-Identifier: FSL-1.1-ALv2

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(espressif_identifiers)
target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_INFUSE_SDK_MODULE_DIR}/lib/vendor/espressif/identifiers.c
)
target_include_directories(app PRIVATE src)
target_compile_definitions(app PRIVATE CONFIG_INFUSE_PROVISIONING_ESP32_EFUSE=1)
