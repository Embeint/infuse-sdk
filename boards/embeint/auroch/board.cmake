# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_AUROCH_NRF9151 OR CONFIG_BOARD_AUROCH_NRF9151_NS)
  if(CONFIG_BOARD_AUROCH_NRF9151_NS)
    set(TFM_PUBLIC_KEY_FORMAT "full")
  endif()

  if(CONFIG_TFM_FLASH_MERGED_BINARY)
    set_property(TARGET runners_yaml_props_target PROPERTY hex_file tfm_merged.hex)
  endif()

  board_runner_args(jlink "--device=nRF9151_xxCA" "--speed=4000")
  include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
  include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
elseif(CONFIG_SOC_NRF54L15_CPUAPP)
  board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")
  include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
  include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
endif()
