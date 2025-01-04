# Infuse-IoT sysbuild integration

if (NOT ${SB_CONFIG_INFUSE_INTEGRATION})
  # Not Infuse sysbuild, exit
  return()
endif()

if (${SB_CONFIG_BOOTLOADER_MCUBOOT})
  # Infuse snippet should be applied to MCUBoot application
  set(mcuboot_SNIPPET "infuse" CACHE INTERNAL "")
  # Ensure multithreading is always enabled in MCUBoot
  # External flash drivers and USB support depend on it
  set(mcuboot_CONFIG_MULTITHREADING "y" CACHE INTERNAL "")
  if (${SB_CONFIG_SOC_FAMILY_NORDIC_NRF})
    # External QSPI support
    set(mcuboot_CONFIG_NORDIC_QSPI_NOR "y" CACHE INTERNAL "")
    # Support images up to 1MB in size
    set(mcuboot_CONFIG_BOOT_MAX_IMG_SECTORS 256 CACHE INTERNAL "")
  endif()
  # Notify DTS that this is a bootloader build
  set(mcuboot_DTS_EXTRA_CPPFLAGS "-DINFUSE_IS_BOOTLOADER=1" CACHE INTERNAL "")
endif()

if(SB_CONFIG_DOMAIN_CPUNET_BOARD)
  set(NET_APP hci_ipc)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/${NET_APP})

  ExternalZephyrProject_Add(
    APPLICATION ${NET_APP}
    SOURCE_DIR  ${NET_APP_SRC_DIR}
    BOARD       ${SB_CONFIG_DOMAIN_CPUNET_BOARD}
  )

  set(${NET_APP}_CONF_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/${NET_APP}/infuse.conf
    CACHE INTERNAL ""
  )
  sysbuild_add_dependencies(FLASH mcuboot ${NET_APP})
endif()
