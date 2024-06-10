# Infuse-IoT vendor specific libraries

add_subdirectory_ifdef(CONFIG_SOC_FAMILY_NRF nordic)
add_subdirectory_ifdef(CONFIG_SOC_POSIX posix)
add_subdirectory_ifdef(CONFIG_QEMU_TARGET qemu)
