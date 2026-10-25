# Infuse-IoT constrained binary patching libraries

infuse_sources(patch.c)
infuse_sources_ifdef(CONFIG_INFUSE_CPATCH_ENCODER_SHORTEST_PATH patch_shortest_path.c)
