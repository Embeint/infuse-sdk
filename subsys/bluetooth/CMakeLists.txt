# Infuse-IoT common Bluetooth integration

if(CONFIG_BT_HCI_HOST)
  zephyr_sources_ifdef(CONFIG_BT_PERIPHERAL gatt.c)
endif()
