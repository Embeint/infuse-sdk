# Infuse-IoT common Bluetooth integration

if(CONFIG_BT_HCI_HOST)
  infuse_sources_ifdef(CONFIG_BT_PERIPHERAL gatt.c)
  infuse_sources_ifdef(CONFIG_BT_PERIPHERAL legacy_adv.c)
endif()

infuse_sources_ifdef(CONFIG_BT_CONTROLLER_MANAGER controller_manager.c)
infuse_sources_ifdef(CONFIG_BT_INFUSE_ASSERT_HANDLER assert_handler.c)
