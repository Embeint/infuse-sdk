# Infuse-IoT Memfault integration

zephyr_include_directories_ifdef(CONFIG_MEMFAULT_INFUSE_CONFIG ${INFUSE_DIR}/include/infuse/lib/memfault_config)

zephyr_sources(infuse_memfault.c)
zephyr_sources_ifdef(CONFIG_MEMFAULT_INFUSE_CONFIG infuse_metrics.c)

# Manually override the version defines, otherwise Memfault will see ncs_version.h
# and assume that the build is a ncs application.
zephyr_compile_definitions(MEMFAULT_OS_VERSION_NAME="infuse-iot")
zephyr_compile_definitions(MEMFAULT_OS_VERSION_STRING="${CMAKE_PROJECT_VERSION}")
