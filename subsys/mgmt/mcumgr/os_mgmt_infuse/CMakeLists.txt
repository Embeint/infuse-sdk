# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(os_mgmt_infuse.c)

if (CONFIG_BOOTLOADER_MCUBOOT)
  zephyr_include_directories(
    ${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/bootutil/include
  )
endif()
