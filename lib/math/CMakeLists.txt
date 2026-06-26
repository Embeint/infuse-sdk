# Infuse-IoT math libraries compilation

infuse_sources(common.c)
infuse_sources(cartesian.c)
infuse_sources(statistics.c)

infuse_sources_ifdef(CONFIG_INFUSE_MATH_GEODESY geodesy.c)
infuse_sources_ifdef(CONFIG_INFUSE_MATH_HAMMING hamming_8_4.c)
