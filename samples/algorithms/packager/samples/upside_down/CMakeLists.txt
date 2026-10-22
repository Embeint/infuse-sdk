# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(INFUSE_WORKSPACE ${CMAKE_CURRENT_LIST_DIR}/../../../../../..)
set(INFUSE_SDK_BASE ${INFUSE_WORKSPACE}/infuse-sdk)
set(ZEPHYR_BASE ${INFUSE_WORKSPACE}/zephyr)

include(${INFUSE_SDK_BASE}/subsys/algorithms/packager/algorithm_build.cmake)

project(UpsideDownAlgorithm LANGUAGES C)

algorithm_generate_targets(upside_down
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/main.c
  OUTPUT_BASE ${PROJECT_BINARY_DIR}
)
