# Infuse-IoT networking compilation

add_subdirectory_ifdef(CONFIG_NET_CONNECTION_MANAGER conn_mgr)

add_subdirectory(dns)
add_subdirectory(sntp)
