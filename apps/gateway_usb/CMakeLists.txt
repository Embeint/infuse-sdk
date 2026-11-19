# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Common serial gateway configuration
include(${CMAKE_CURRENT_LIST_DIR}/../common/gateway_serial/gateway.cmake)

if(NOT usb IN_LIST SNIPPET)
  # Force USB snippet to end to overwrite infuse defaults
  set(SNIPPET ${SNIPPET} usb CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gateway_usb)

target_sources(app PRIVATE
  ${GATEWAY_COMMON_DIR}/src/main.c
)
