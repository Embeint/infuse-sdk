# Infuse-IoT Memfault integration

zephyr_include_directories_ifdef(CONFIG_MEMFAULT_INFUSE_CONFIG ${INFUSE_DIR}/include/infuse/lib/memfault_config)

zephyr_sources(infuse_memfault.c)
