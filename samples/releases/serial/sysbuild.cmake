# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_BOARD STREQUAL "nucleo_l432kc")
  # Minimal ROM MCUboot to fit the reduced partition
  set(mcuboot_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_minimal_stm32l4.conf" CACHE INTERNAL "")
endif()
