# Infuse-IoT DFU libraries

infuse_sources_ifdef(CONFIG_INFUSE_DFU_EXFAT dfu_exfat.c)
infuse_sources_ifdef(CONFIG_INFUSE_DFU_HELPERS dfu_helpers.c)
