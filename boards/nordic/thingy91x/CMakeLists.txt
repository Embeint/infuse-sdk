# Kconfig - nRF91 Thingy:91 X board configuration
#
# Copyright (c) 2024 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

if(CONFIG_WIFI_NRF70)
  if(CONFIG_BOARD_THINGY91X_NRF9151 OR CONFIG_BOARD_THINGY91X_NRF9151_NS)
    zephyr_library()
    zephyr_library_sources(nrf70_support.c)
  endif()
endif()
