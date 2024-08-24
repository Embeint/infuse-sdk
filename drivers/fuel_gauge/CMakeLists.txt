# Battery fuel-gauge drivers

zephyr_library_sources_ifdef(CONFIG_FUEL_GAUGE_COMPOSITE fuel_gauge_composite.c)
