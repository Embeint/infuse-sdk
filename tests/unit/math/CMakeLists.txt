# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

project(infuse_math)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
target_include_directories(testbinary PRIVATE ${ZEPHYR_BASE}/../infuse-sdk/include)
target_link_libraries(testbinary PRIVATE -lm)
target_sources(testbinary PRIVATE
  main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/math/common.c
)
