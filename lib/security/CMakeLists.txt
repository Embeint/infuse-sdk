# Infuse-IoT Security compilation

zephyr_library()
zephyr_library_sources(security.c)

add_subdirectory(hardware_unique_key)
