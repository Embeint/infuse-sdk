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
  # Support images up to 1MB in size
  set(mcuboot_CONFIG_BOOT_MAX_IMG_SECTORS 256 CACHE INTERNAL "")
endif()
