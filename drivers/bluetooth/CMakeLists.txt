# SPDX-License-Identifier: FSL-1.1-ALv2

if(CONFIG_BT_HCI_CYW43_INFUSE)
  include(cyw43_resources.cmake)
  set(firmware_dir ${INFUSE_DIR}/zephyr/blobs/img)
  if(NOT EXISTS ${firmware_dir}/cyw43_btfw_43439.h)
    message(FATAL_ERROR "CYW43439 Bluetooth firmware missing: run west blobs fetch infuse-sdk")
  endif()
  set(whd_root ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/whd-expansion/WHD)
  zephyr_library_named(infuse_cyw43_hci)
  zephyr_library_sources(cyw43_hci.c cyw43_bus.c cyw43_transport.c)
  zephyr_library_include_directories(
    ${firmware_dir}
    ${whd_root}/COMMON/inc
    ${whd_root}/COMPONENT_WIFI5/inc
    ${whd_root}/COMPONENT_WIFI5/src/include
    ${whd_root}/COMPONENT_WIFI5/src/bus_protocols
    ${ZEPHYR_BASE}/drivers/wifi/infineon
  )
  # WHD assumes a single bus caller. Guard backplane-window selection and
  # the full HAL transfer (including shared data/IRQ pin switching).
  foreach(symbol whd_bus_spi_transfer whd_bus_read_backplane_value
      whd_bus_write_backplane_value whd_bus_transfer_backplane_bytes
      whd_bus_sleep whd_bus_wakeup whd_ensure_wlan_bus_is_up
      whd_allow_wlan_bus_to_sleep whd_bus_send_buffer whd_bus_read_frame
      whd_bus_packet_available_to_read whd_bus_set_backplane_window)
    zephyr_ld_options(-Wl,--wrap=${symbol})
  endforeach()
endif()
