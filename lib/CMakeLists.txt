# Infuse-IoT libraries compilation

infuse_sources(identifiers.c)
infuse_sources(version.c)
infuse_sources_ifdef(CONFIG_INFUSE_WORKQ infuse_work_q.c)
infuse_sources_ifdef(CONFIG_INFUSE_REBOOT_COMPILE reboot.c)
infuse_sources_ifdef(CONFIG_INFUSE_COMMON_BOOT common_boot.c)
if(CONFIG_INFUSE_COMMON_BOOT AND CONFIG_KV_STORE_KEY_Z_DEVSTATE)
  # Link time check that the device state array fits the reflected value limit
  zephyr_linker_sources(SECTIONS common_boot_z_devstate.ld)
endif()
infuse_sources_ifdef(CONFIG_INFUSE_EPOCH_TIME time/epoch.c)

add_subdirectory(auto)
add_subdirectory(crypto)
add_subdirectory(debug)
add_subdirectory(states)
add_subdirectory_ifdef(CONFIG_INFUSE_MATH math)
add_subdirectory_ifdef(CONFIG_INFUSE_SDK vendor)
add_subdirectory_ifdef(CONFIG_INFUSE_SECURITY security)
add_subdirectory_ifdef(CONFIG_INFUSE_MEMFAULT memfault)
add_subdirectory_ifdef(CONFIG_INFUSE_MODEM_MONITOR modem_monitor)
