# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(algorithm_build_system)

set(algorithm_src ${CMAKE_CURRENT_LIST_DIR}/test_algorithm/main.c)
if(CONFIG_TEST_ALGORITHM_BUILD_LLEXT)
  # Setup algorithm build
  set(INFUSE_SDK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../../..)
  include(${INFUSE_SDK_BASE}/subsys/algorithms/algorithm_build.cmake)
  set(algorithm_name test_algorithm)
  set(algorithm_base_dir "${PROJECT_BINARY_DIR}/llext")
  set(algorithm_inc ${CMAKE_CURRENT_LIST_DIR}/include)
  algorithm_generate_targets(${algorithm_name} ${algorithm_src} ${algorithm_inc} ${algorithm_base_dir})
  # Add algorithm as application dependency
  algorithm_target_name(${algorithm_name} algorithm_target)
  add_dependencies(app ${algorithm_target})
  # Add directory containing the raw hex of the algorithm to the include path
  algorithm_target_dir(${algorithm_base_dir} algorithm_dir)
  zephyr_include_directories(${algorithm_dir})
else()
  # Add the algorithm implementation directly
  set(algorithm_output ${algorithm_src})
endif()

zephyr_include_directories(include)
target_sources(app PRIVATE
  src/main.c
  ${algorithm_output}
)
