# Infuse Remote Procedure Call compilation

zephyr_library_named(infuse_rpc)

zephyr_library_sources(server.c)
zephyr_library_sources(command_runner.c)
add_subdirectory(commands)
