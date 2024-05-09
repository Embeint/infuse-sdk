# Infuse IoT libraries compilation

zephyr_sources_ifdef(CONFIG_INFUSE_SDK identifiers.c)

zephyr_sources_ifdef(CONFIG_INFUSE_REBOOT reboot.c)

zephyr_sources_ifdef(CONFIG_INFUSE_COMMON_BOOT common_boot.c)

zephyr_sources_ifdef(CONFIG_INFUSE_CIVIL_TIME time/civil.c)

add_subdirectory_ifdef(CONFIG_CRYPTO_ASCON crypto/ascon)
