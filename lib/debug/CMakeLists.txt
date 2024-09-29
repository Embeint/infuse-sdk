# Infuse-IoT debugging libraries

zephyr_sources_ifdef(CONFIG_INFUSE_WORKQUEUE_BLOCK_TEST workqueue_blocker.c)

if(CONFIG_INFUSE_WORKQUEUE_BLOCK_TEST)
  message(WARNING
  "CONFIG_INFUSE_WORKQUEUE_BLOCK_TEST is enabled for stability testing purposes!
  This is not suitable for production")
endif()
