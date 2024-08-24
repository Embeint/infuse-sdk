# Infuse-IoT drivers

add_subdirectory_ifdef(CONFIG_FUEL_GAUGE fuel_gauge)
add_subdirectory_ifdef(CONFIG_GNSS gnss)
add_subdirectory_ifdef(CONFIG_INFUSE_IMU imu)
add_subdirectory_ifdef(CONFIG_INFUSE_WATCHDOG watchdog)
