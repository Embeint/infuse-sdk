# Infuse-IoT GNSS drivers

zephyr_library()
if (NOT CONFIG_GNSS_UBX_MODEM_EMUL)
  zephyr_library_sources_ifdef(CONFIG_GNSS_UBX_MODEM gnss_ubx_common.c)
  zephyr_library_sources_ifdef(CONFIG_GNSS_UBX_MODEM gnss_ubx_modem.c)
endif()
zephyr_library_sources_ifdef(CONFIG_GNSS_U_BLOX_M10_I2C gnss_u_blox_m10_i2c.c)
zephyr_library_sources_ifdef(CONFIG_GNSS_U_BLOX_M8_SPI gnss_u_blox_m8_spi.c)

zephyr_library_sources_ifdef(CONFIG_GNSS_NRF9X gnss_nrf9x.c)

# Emulated drivers
zephyr_library_sources_ifdef(CONFIG_GNSS_U_BLOX_M8_EMUL gnss_u_blox_m8_emul.c)
