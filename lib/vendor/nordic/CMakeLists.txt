# Infuse-IoT Nordic specific implementations

zephyr_sources_ifndef(CONFIG_INFUSE_CUSTOM_IDENTIFIERS identifiers.c)
if(CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE)
  zephyr_sources_ifdef(CONFIG_SOC_SERIES_NRF52X dap_disable_nrf52.c)
  zephyr_sources_ifdef(CONFIG_SOC_SERIES_NRF54LX dap_disable_nrf54l.c)
endif()
