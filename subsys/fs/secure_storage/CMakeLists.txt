# Infuse IoT secure storage compilation

zephyr_include_directories(include)

zephyr_sources(secure_storage.c)
zephyr_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)
