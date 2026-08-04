# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Infuse-IoT application
if(NOT infuse IN_LIST SNIPPET)
  set(SNIPPET infuse ${SNIPPET} CACHE STRING "" FORCE)
endif()

if(NOT DEFINED KCONFIG_BINARY_DIR)
  set(KCONFIG_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/Kconfig)
endif()

find_program(WEST_EXECUTABLE west REQUIRED)

execute_process(
  COMMAND ${WEST_EXECUTABLE} cloudgen
    -d ${CMAKE_CURRENT_LIST_DIR}/extensions
    -o ${KCONFIG_BINARY_DIR}
    --skip-python-generation
  RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
  message(FATAL_ERROR "cloudgen failed: ${ret}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(application_states)

# Use extended definitions generated into the build directory before Kconfig runs.
zephyr_include_directories(${KCONFIG_BINARY_DIR}/generated/include include)
# Compile in the extended RPC commands
zephyr_sources(${KCONFIG_BINARY_DIR}/generated/rpc_command_runner.c)

target_sources(app PRIVATE
  src/main.c
)
