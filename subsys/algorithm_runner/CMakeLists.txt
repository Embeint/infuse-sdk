# Infuse-IoT algorithm abstraction

infuse_sources(runner.c)
infuse_sources_ifdef(CONFIG_ALGORITHM_RUNNER_LLEXT_LITTLEFS_LOADER llext_littlefs_loader.c)

add_subdirectory(algorithms)
