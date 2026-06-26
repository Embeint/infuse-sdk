# Infuse-IoT sensor drivers

infuse_sources_ifdef(CONFIG_BQ25798 bq25798.c)
infuse_sources_ifdef(CONFIG_NRF9X_TEMP nrf9x_temp.c)
infuse_sources_ifdef(CONFIG_NRF9X_BATT nrf9x_batt.c)
infuse_sources_ifdef(CONFIG_PAC194X pac194x.c)
infuse_sources_ifdef(CONFIG_SENSOR_GENERIC_SIM generic_sim.c)
