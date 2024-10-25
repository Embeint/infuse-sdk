# Infuse-IoT DFU libraries

zephyr_sources_ifdef(CONFIG_INFUSE_DFU_EXFAT dfu_exfat.c)
zephyr_sources_ifdef(CONFIG_INFUSE_DFU_HELPERS dfu_helpers.c)
