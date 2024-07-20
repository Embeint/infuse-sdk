# Infuse-IoT drivers

add_subdirectory_ifdef(CONFIG_INFUSE_IMU imu)
add_subdirectory_ifdef(CONFIG_INFUSE_WATCHDOG watchdog)
