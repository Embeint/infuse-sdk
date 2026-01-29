# Infuse-IoT LittleFS compilation

zephyr_library()

zephyr_library_sources(littlefs.c)
zephyr_library_sources(littlefs_util.c)
zephyr_library_sources(littlefs_flash_area.c)
zephyr_library_link_libraries(LITTLEFS)
