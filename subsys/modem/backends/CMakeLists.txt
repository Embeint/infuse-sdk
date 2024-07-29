# Infuse IoT custom modem backends

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MODEM_BACKEND_U_BLOX_I2C modem_backend_u_blox_i2c.c)
