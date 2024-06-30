# Infuse-IoT subsys compilation

add_subdirectory_ifdef(CONFIG_DATA_LOGGER data_logger)
add_subdirectory_ifdef(CONFIG_EPACKET epacket)
add_subdirectory_ifdef(CONFIG_TDF tdf)
add_subdirectory_ifdef(CONFIG_INFUSE_RPC rpc)
add_subdirectory_ifdef(CONFIG_TASK_RUNNER task_runner)

add_subdirectory(fs)
add_subdirectory(net)
