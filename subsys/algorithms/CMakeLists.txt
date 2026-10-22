# Infuse-IoT algorithm abstraction

# Algorithm loader (& symbol exports for loaded algorithm)
infuse_sources_ifdef(CONFIG_ALGORITHM_LOADER_LLEXT_LITTLEFS_LOADER loader/llext_littlefs_loader.c)
infuse_sources_ifdef(CONFIG_ALGORITHM_PACKAGER loader/algorithm_exports.c)

# Algorithm runner
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER runner/runner.c)

# Included Infuse-IoT sample algorithms
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER_ALG_DEMO samples/alg_demo.c)
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER_ALG_STATIONARY_WINDOWED samples/alg_stationary_windowed.c)
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER_ALG_TILT samples/alg_tilt.c)
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER_ALG_MOVEMENT_THRESHOLD samples/alg_movement_threshold.c)
