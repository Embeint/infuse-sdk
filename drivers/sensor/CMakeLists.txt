# Infuse-IoT sensor drivers

zephyr_sources_ifdef(CONFIG_BQ25798 bq25798.c)
zephyr_sources_ifdef(CONFIG_NRF9X_TEMP nrf9x_temp.c)
zephyr_sources_ifdef(CONFIG_NRF9X_BATT nrf9x_batt.c)
zephyr_sources_ifdef(CONFIG_SENSOR_GENERIC_SIM generic_sim.c)
