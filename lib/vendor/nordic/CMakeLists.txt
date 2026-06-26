# Infuse-IoT Nordic specific implementations

infuse_sources_ifndef(CONFIG_INFUSE_CUSTOM_IDENTIFIERS identifiers.c)
if(CONFIG_INFUSE_COMMON_BOOT_DEBUG_PORT_DISABLE)
  infuse_sources_ifdef(CONFIG_SOC_SERIES_NRF52 dap_disable_nrf52.c)
  infuse_sources_ifdef(CONFIG_SOC_SERIES_NRF54L dap_disable_nrf54l.c)
endif()
