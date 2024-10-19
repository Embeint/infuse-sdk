# Infuse IoT Hardware Unique Key configuration

zephyr_sources_ifdef(CONFIG_INFUSE_HUK_TFM huk_tfm.c)
zephyr_sources_ifdef(CONFIG_INFUSE_HUK_NORDIC_KMU huk_nrf_kmu.c)
zephyr_sources_ifdef(CONFIG_INFUSE_HUK_HWID_HASH huk_hwid_hash.c)
