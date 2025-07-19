# Infuse-IoT sysbuild integration

if(NOT ${SB_CONFIG_INFUSE_INTEGRATION})
  # Not Infuse sysbuild, exit
  return()
endif()

if(${SB_CONFIG_BOOTLOADER_MCUBOOT})
  if(${SB_CONFIG_INFUSE_INTEGRATION_BT_CTLR})
    # infuse-bt-btlr snippet should be applied to MCUBoot application
    set(mcuboot_SNIPPET "infuse-bt-ctlr" CACHE INTERNAL "")
  else()
    # infuse snippet should be applied to MCUBoot application
    set(mcuboot_SNIPPET "infuse" CACHE INTERNAL "")
  endif()
  # Ensure multithreading is always enabled in MCUBoot
  # External flash drivers and USB support depend on it
  set(mcuboot_CONFIG_MULTITHREADING "y" CACHE INTERNAL "")
  if (${SB_CONFIG_SOC_FAMILY_NORDIC_NRF})
    # External QSPI support
    set(mcuboot_CONFIG_NORDIC_QSPI_NOR "y" CACHE INTERNAL "")
  endif()
  # Notify DTS that this is a bootloader build
  set(mcuboot_DTS_EXTRA_CPPFLAGS "-DINFUSE_IS_BOOTLOADER=1" CACHE INTERNAL "")
endif()

if(SB_CONFIG_BT_HCI_IPC_BOARD_TARGET_CPUCLUSTER)
  set(NET_APP hci_ipc)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/${NET_APP})
  set(NET_APP_BOARD "${BOARD}/${SB_CONFIG_SOC}/${SB_CONFIG_BT_HCI_IPC_BOARD_TARGET_CPUCLUSTER}")

  ExternalZephyrProject_Add(
    APPLICATION ${NET_APP}
    SOURCE_DIR  ${NET_APP_SRC_DIR}
    BOARD       ${NET_APP_BOARD}
  )

  set(${NET_APP}_CONF_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/${NET_APP}/infuse.conf
    CACHE INTERNAL "Infuse-IoT HCI IPC configuration file"
  )
  set(${NET_APP}_DTC_OVERLAY_FILE
      ${CMAKE_CURRENT_SOURCE_DIR}/${NET_APP}/infuse.overlay
    CACHE INTERNAL "Infuse-IoT HCI IPC devicetree overlay"
  )
  if(${SB_CONFIG_BOOTLOADER_MCUBOOT})
    sysbuild_add_dependencies(FLASH mcuboot ${NET_APP})
  endif()
endif()
