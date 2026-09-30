# Infuse-IoT WiFi sources

infuse_sources_ifdef(CONFIG_WIFI_SIM wifi_sim.c)

# The pinned AIROC release bypasses net_buf_unref, breaking pool accounting.
if(CONFIG_WIFI_AIROC AND CONFIG_NET_BUF_POOL_USAGE)
  include(airoc_buffer_accounting.cmake)
endif()
