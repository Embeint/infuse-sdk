# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(NOT infuse IN_LIST SNIPPET)
  set(SNIPPET infuse ${SNIPPET} CACHE STRING "" FORCE)
endif()
if(NOT usb IN_LIST SNIPPET)
  set(SNIPPET ${SNIPPET} usb CACHE STRING "" FORCE)
endif()

if((BOARD STREQUAL "rpi_pico/rp2040/w" OR
    BOARD STREQUAL "rpi_pico2/rp2350a/m33/w") AND
   NOT infuse-pico-wireless IN_LIST SNIPPET)
  set(SNIPPET ${SNIPPET} infuse-pico-wireless CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(epacket_wireless)

target_sources(app PRIVATE src/main.c)
