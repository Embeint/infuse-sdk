# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(INFUSE_WORKSPACE ${CMAKE_CURRENT_LIST_DIR}/../../../../../../..)
set(INFUSE_SDK_BASE ${INFUSE_WORKSPACE}/infuse-sdk)
set(ZEPHYR_BASE ${INFUSE_WORKSPACE}/zephyr)
set(ZEPHYR_SDK_EDGE_AI_MODULE_DIR
  "${INFUSE_WORKSPACE}/modules/nrfconnect/sdk-edge-ai"
  CACHE PATH "Path to the sdk-edge-ai module"
)

include(${INFUSE_SDK_BASE}/subsys/algorithms/packager/algorithm_build.cmake)

project(ImuGesturesAlgorithm LANGUAGES C)

set(GESTURE_MODEL_DIR ${CMAKE_CURRENT_LIST_DIR}/nrf_edgeai_generated)
set(GESTURE_MODEL_MACROS_DIR ${PROJECT_BINARY_DIR}/generated)
set(GESTURE_MODEL_MACROS_H ${GESTURE_MODEL_MACROS_DIR}/model_macros.h)
include(${INFUSE_SDK_BASE}/subsys/algorithms/packager/nrf_edge_ai/export_model_macros.cmake)
export_model_macros(${GESTURE_MODEL_DIR} ${GESTURE_MODEL_MACROS_H})

set(GESTURE_MODEL_C ${GESTURE_MODEL_DIR}/nrf_edgeai_user_model.c)

algorithm_generate_targets(gesture_recognition
  NRF_EDGEAI
  SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${GESTURE_MODEL_C}
    ${GESTURE_MODEL_MACROS_H}
  INCLUDES
    ${GESTURE_MODEL_MACROS_DIR}
  OUTPUT_BASE ${PROJECT_BINARY_DIR}
)
