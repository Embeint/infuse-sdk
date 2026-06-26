# Infuse-IoT WiFi connectivity implementations

if(CONFIG_CONN_MGR_WIFI_KV_STORE)
  zephyr_library_named(infuse_net_conn_mgr)
  zephyr_library_sources(conn_mgr_wifi_kv_store.c)

  if(CONFIG_CONN_MGR_WIFI_KV_STORE_PRECOMPUTE_PBKDF2)
    # Implementation requires the hostap include paths
    zephyr_library_link_libraries(hostap)
  endif()
endif()
