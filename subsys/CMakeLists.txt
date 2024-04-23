# EIS subsys compilation

add_subdirectory_ifdef(CONFIG_EPACKET epacket)
add_subdirectory_ifdef(CONFIG_TDF tdf)

add_subdirectory(fs)
