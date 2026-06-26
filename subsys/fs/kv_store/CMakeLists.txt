# Infuse-IoT key-value store compilation

infuse_sources(
  kv_store_common.c
  kv_reflect.c
)
infuse_sources_ifdef(CONFIG_KV_STORE_NVS kv_store_nvs.c)
infuse_sources_ifdef(CONFIG_KV_STORE_ZMS kv_store_zms.c)
