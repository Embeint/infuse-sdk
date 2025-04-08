# Infuse-IoT key-value store compilation

zephyr_sources(
  kv_store_common.c
  kv_reflect.c
)
zephyr_sources_ifdef(CONFIG_KV_STORE_NVS kv_store_nvs.c)
zephyr_sources_ifdef(CONFIG_KV_STORE_ZMS kv_store_zms.c)
