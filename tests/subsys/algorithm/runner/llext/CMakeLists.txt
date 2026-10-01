# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(algorithm_littlefs)

set(INFUSE_SDK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(algorithm_base_dir "${PROJECT_BINARY_DIR}/llext")

include(${INFUSE_SDK_BASE}/subsys/algorithms/algorithm_build.cmake)
foreach(algorithm_name test_algorithm test_algorithm_2 test_algorithm_invalid_version)
  algorithm_generate_targets(${algorithm_name}
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/${algorithm_name}/main.c
    OUTPUT_BASE ${algorithm_base_dir}
  )

  # Only build and embed the algorithms here. Installing them in LittleFS is an
  # explicit runtime operation performed by the test helpers.
  algorithm_target_name(${algorithm_name} algorithm_target)
  algorithm_target_dir(${algorithm_base_dir} algorithm_dir)
  algorithm_target_inc(${algorithm_name} ${algorithm_base_dir} algorithm_inc_file)
  add_dependencies(app ${algorithm_target})
  set_property(SOURCE src/test_algorithm.c APPEND PROPERTY OBJECT_DEPENDS ${algorithm_inc_file})
endforeach()
zephyr_include_directories(${algorithm_dir})

target_sources(app PRIVATE
  src/main.c
  src/test_algorithm.c
)
