# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Infuse-IoT application
if(NOT infuse IN_LIST SNIPPET)
  set(SNIPPET infuse ${SNIPPET} CACHE STRING "" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(algorithms_loader_imu_gestures)

set(INFUSE_SDK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../../..)

if(CONFIG_BUNDLE_PACKAGED_ALGORITHM OR CONFIG_BUNDLE_BUILTIN_ALGORITHM)
  # Configure bundled Algorithm build
  set(ALGORITHM_DIR ${CMAKE_CURRENT_LIST_DIR}/../../packager/samples/nrf_edge_ai/imu_gestures)
  set(algorithm_src
    ${ALGORITHM_DIR}/main.c
    ${ALGORITHM_DIR}/nrf_edgeai_generated/nrf_edgeai_user_model.c
  )
  set(algorithm_inc
    ${CMAKE_CURRENT_LIST_DIR}/include
  )

  set(GESTURE_MODEL_DIR ${ALGORITHM_DIR}/nrf_edgeai_generated)
  set(GESTURE_MODEL_MACROS_H ${ZEPHYR_BINARY_DIR}/include/generated/infuse/model_macros.h)

  include(${INFUSE_SDK_BASE}/subsys/algorithms/packager/nrf_edge_ai/export_model_macros.cmake)
  export_model_macros(${GESTURE_MODEL_DIR}
    ${ZEPHYR_BINARY_DIR}/include/generated/infuse/model_macros.h
  )

  list(APPEND algorithm_src ${GESTURE_MODEL_MACROS_H})
  list(APPEND algorithm_inc ${ZEPHYR_BINARY_DIR}/include/generated/infuse)
endif()

if(CONFIG_BUNDLE_PACKAGED_ALGORITHM)
  # Bundle algorithm via LLEXT
  include(${INFUSE_SDK_BASE}/subsys/algorithms/packager/algorithm_build.cmake)
  set(algorithm_name gesture_algorithm)
  set(algorithm_base_dir "${PROJECT_BINARY_DIR}/llext")
  algorithm_generate_targets(${algorithm_name}
    NRF_EDGEAI
    SOURCES ${algorithm_src}
    INCLUDES ${algorithm_inc}
    OUTPUT_BASE ${algorithm_base_dir}
  )
  # Add algorithm as application dependency
  algorithm_target_name(${algorithm_name} algorithm_target)
  add_dependencies(app ${algorithm_target})
  # Add directory containing the raw hex of the algorithm to the include path
  algorithm_target_dir(${algorithm_base_dir} algorithm_dir)
  algorithm_target_inc(${algorithm_name} ${algorithm_base_dir} algorithm_inc_file)
  set_property(SOURCE src/main.c APPEND PROPERTY OBJECT_DEPENDS ${algorithm_inc_file})
  zephyr_include_directories(
    ${algorithm_dir}
  )
elseif(CONFIG_BUNDLE_BUILTIN_ALGORITHM)
  # Add the algorithm implementation directly
  set(algorithm_output ${algorithm_src})
  zephyr_include_directories(${algorithm_inc})
endif()

zephyr_include_directories(include)
target_sources(app PRIVATE
  src/main.c
  ${algorithm_output}
)
