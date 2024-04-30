# Data logger compilation

zephyr_sources(data_logger.c)
zephyr_sources_ifdef(CONFIG_DATA_LOGGER_FLASH_MAP backends/flash_map.c)
zephyr_sources_ifdef(CONFIG_DATA_LOGGER_EPACKET backends/epacket.c)
