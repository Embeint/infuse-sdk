# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Embedded Intelligence SDK application
if(NOT eis IN_LIST SNIPPET)
  set(SNIPPET eis ${SNIPPET} CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crypto_profiling)

target_sources(app PRIVATE
  src/main.c
)
