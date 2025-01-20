# Infuse-IoT sensor drivers

zephyr_sources_ifdef(CONFIG_NRF9X_TEMP nrf9x_temp.c)
zephyr_sources_ifdef(CONFIG_NRF9X_BATT nrf9x_batt.c)
