# Infuse-IoT common Bluetooth integration

if(CONFIG_BT_HCI_HOST)
  zephyr_sources_ifdef(CONFIG_BT_PERIPHERAL gatt.c)
  zephyr_sources_ifdef(CONFIG_BT_PERIPHERAL legacy_adv.c)
endif()

zephyr_sources_ifdef(CONFIG_BT_CONTROLLER_MANAGER controller_manager.c)
zephyr_sources_ifdef(CONFIG_BT_INFUSE_ASSERT_HANDLER assert_handler.c)
