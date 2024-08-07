# Infuse-IoT validation subsystem

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_INFUSE_IMU validation_imu.c)
