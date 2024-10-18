# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpatch)

target_compile_definitions(app PUBLIC BIN_HELLO_WORLD=${CMAKE_CURRENT_LIST_DIR}/bin/hello_world.bin)
target_compile_definitions(app PUBLIC BIN_PHILOSOPHERS=${CMAKE_CURRENT_LIST_DIR}/bin/philosophers.bin)
target_compile_definitions(app PUBLIC PATCH_HELLO_TO_HELLO=${CMAKE_CURRENT_LIST_DIR}/patch/hello_world-to-hello_world.bin)
target_compile_definitions(app PUBLIC PATCH_HELLO_VALIDATION=${CMAKE_CURRENT_LIST_DIR}/patch/hello_world-validate.bin)
target_compile_definitions(app PUBLIC PATCH_HELLO_BAD_LEN=${CMAKE_CURRENT_LIST_DIR}/patch/hello_world-bad-len.bin)
target_compile_definitions(app PUBLIC PATCH_HELLO_BAD_CRC=${CMAKE_CURRENT_LIST_DIR}/patch/hello_world-bad-crc.bin)
target_compile_definitions(app PUBLIC PATCH_HELLO_TO_PHILO=${CMAKE_CURRENT_LIST_DIR}/patch/hello_world-to-philosophers.bin)
target_compile_definitions(app PUBLIC PATCH_PHILO_TO_HELLO=${CMAKE_CURRENT_LIST_DIR}/patch/philosophers-to-hello_world.bin)

target_sources(app PRIVATE
  src/main.c
)
