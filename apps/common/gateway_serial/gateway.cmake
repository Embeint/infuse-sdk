# SPDX-License-Identifier: Apache-2.0
#
# Common serial Bluetooth gateway application configuration.
# Must be included before `find_package(Zephyr)`.

set(GATEWAY_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

# Infuse-IoT application
if(NOT infuse IN_LIST SNIPPET)
  set(SNIPPET infuse ${SNIPPET} CACHE STRING "" FORCE)
endif()

# Common configuration. Zephyr merges this local value with any user provided
# `EXTRA_CONF_FILE` (cache, sysbuild, snippets), with this file applied first.
set(EXTRA_CONF_FILE ${GATEWAY_COMMON_DIR}/gateway.conf)
