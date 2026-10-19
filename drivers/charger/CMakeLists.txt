# Battery charger drivers

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_CHARGER_BQ25798 bq25798.c)
