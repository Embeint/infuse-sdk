# Infuse-IoT math libraries compilation

zephyr_sources(common.c)
zephyr_sources(cartesian.c)
zephyr_sources(statistics.c)

zephyr_sources_ifdef(CONFIG_INFUSE_MATH_GEODESY geodesy.c)
