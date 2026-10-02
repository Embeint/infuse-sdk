# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(INFUSE_SDK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(ZEPHYR_BASE ${INFUSE_SDK_BASE}/../zephyr)
set(ZEPHYR_SDK_EDGE_AI_MODULE_DIR
  "${INFUSE_SDK_BASE}/../modules/nrfconnect/sdk-edge-ai"
  CACHE PATH "Path to the sdk-edge-ai module"
)

include(${INFUSE_SDK_BASE}/subsys/algorithms/algorithm_build.cmake)

project(GestureEdgeAIAlgorithm LANGUAGES C)

algorithm_generate_targets(gesture_recognition
  NRF_EDGEAI
  SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/nrf_edgeai_generated/nrf_edgeai_user_model.c
  OUTPUT_BASE ${PROJECT_BINARY_DIR}
)
