# EIS libraries compilation

zephyr_sources_ifdef(CONFIG_EIS_CIVIL_TIME time/civil.c)

add_subdirectory_ifdef(CONFIG_CRYPTO_ASCON crypto/ascon)
