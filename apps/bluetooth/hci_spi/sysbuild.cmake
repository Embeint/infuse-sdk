# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_BOARD_QUALIFIERS STREQUAL "nrf54l05/cpuapp")
  # MCUboot must use the same partition layout as the application
  sysbuild_cache_set(VAR mcuboot_EXTRA_DTC_OVERLAY_FILE APPEND REMOVE_DUPLICATES
                     "${CMAKE_CURRENT_LIST_DIR}/socs/nrf54l05_cpuapp.overlay")
  # Minimal ROM MCUboot to fit the reduced partition
  set(mcuboot_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/sysbuild/mcuboot_minimal_nrf54l.conf" CACHE INTERNAL "")
endif()
