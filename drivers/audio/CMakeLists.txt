# Infuse-IoT Audio drivers

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_T5838 t5838.c)
