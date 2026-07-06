# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(algorithms_target_filtering)

set(algorithm_src
  ${CMAKE_CURRENT_LIST_DIR}/test_algorithm/main.c
)
set(algorithm_inc
  ${CMAKE_CURRENT_LIST_DIR}/include
)

if(CONFIG_TEST_ALGORITHM_BUILD_LLEXT)
  # Setup algorithm build
  set(INFUSE_SDK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../../..)
  include(${CMAKE_CURRENT_LIST_DIR}/target_filtering.cmake)
  include(${INFUSE_SDK_BASE}/subsys/algorithms/algorithm_build.cmake)
  set(algorithm_name test_algorithm)
  set(algorithm_base_dir "${PROJECT_BINARY_DIR}/llext")
  get_targets_whitelist_only(target_whitelist)
  message(DEBUG "Target whitelist: ${target_whitelist}")
  algorithm_generate_targets(${algorithm_name}
    SOURCES ${algorithm_src}
    INCLUDES ${algorithm_inc}
    OUTPUT_BASE ${algorithm_base_dir}
    TARGET_WHITELIST ${target_whitelist}
  )
  # Add algorithm as application dependency
  algorithm_target_name(${algorithm_name} algorithm_target)
  add_dependencies(app ${algorithm_target})
  # Add directory containing the raw hex of the algorithm to the include path
  algorithm_target_dir(${algorithm_base_dir} algorithm_dir)
  zephyr_include_directories(
    ${algorithm_dir}
  )
else()
  # Add the algorithm implementation directly
  set(algorithm_output ${algorithm_src})
  zephyr_include_directories(${algorithm_inc})
endif()

zephyr_include_directories(include)
target_sources(app PRIVATE
  src/main.c
  ${algorithm_output}
)
