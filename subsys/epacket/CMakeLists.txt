# ePacket compilation

zephyr_library_named(epacket)

zephyr_library_sources(
  epacket.c
  keys.c
)

if(CONFIG_ZTEST)
  zephyr_library_sources(interfaces/epacket_udp_crypt.c)
  zephyr_library_sources(interfaces/epacket_usb_crypt.c)
else()
  zephyr_library_sources_ifdef(CONFIG_EPACKET_INTERFACE_UDP interfaces/epacket_udp.c)
  zephyr_library_sources_ifdef(CONFIG_EPACKET_INTERFACE_UDP interfaces/epacket_udp_crypt.c)
  zephyr_library_sources_ifdef(CONFIG_EPACKET_INTERFACE_USB interfaces/epacket_usb.c)
  zephyr_library_sources_ifdef(CONFIG_EPACKET_INTERFACE_USB interfaces/epacket_usb_crypt.c)
endif()
zephyr_library_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)
