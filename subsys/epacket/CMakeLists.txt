# ePacket compilation

zephyr_library_named(epacket)

zephyr_library_sources(epacket.c)
zephyr_library_sources_ifdef(CONFIG_EPACKET_INTERFACE_USB interfaces/epacket_usb.c)
