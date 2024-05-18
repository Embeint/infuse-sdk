/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/nvs.h>

#include <infuse/fs/kv_store.h>

#include "kv_internal.h"

static struct nvs_fs fs;
static sys_slist_t cb_list;

#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_ID     FIXED_PARTITION_ID(NVS_PARTITION)
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

LOG_MODULE_REGISTER(kv_store, CONFIG_KV_STORE_LOG_LEVEL);

int kv_store_init(void)
{
	const struct flash_area *area;
	struct flash_pages_info info;
	int rc;

	fs.flash_device = NVS_PARTITION_DEVICE;
	fs.offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		LOG_ERR("No page info");
		return rc;
	}
	fs.sector_size = info.size;
	fs.sector_count = NVS_PARTITION_SIZE / info.size;

	rc = nvs_mount(&fs);
	if (rc == -EDEADLK) {
		/* Doesn't look like a filesystem, erase */
		LOG_WRN("No NVS FS detected, resetting");
		flash_area_open(NVS_PARTITION_ID, &area);
		flash_area_erase(area, 0, NVS_PARTITION_SIZE);
		flash_area_close(area);
		/* Try mounting again */
		rc = nvs_mount(&fs);
	}
	return rc;
}

int kv_store_reset(void)
{
	int rc;

	LOG_INF("Resetting KV store");
	rc = nvs_clear(&fs);
	if (rc == 0) {
		rc = nvs_mount(&fs);
	} else {
		LOG_WRN("Failed to reset KV store (%d)", rc);
	}
	return rc;
}

void kv_store_register_callback(struct kv_store_cb *cb)
{
	sys_slist_append(&cb_list, &cb->node);
}

bool kv_store_key_enabled(uint16_t key)
{
	struct key_value_slot_definition *defs;
	size_t num;

	defs = kv_internal_slot_definitions(&num);
	for (size_t i = 0; i < num; i++) {
		if (IN_RANGE(key, defs[i].key, defs[i].key + defs[i].range - 1)) {
			return true;
		}
	}
	return false;
}

ssize_t kv_store_delete(uint16_t key)
{
	struct kv_store_cb *cb;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_enabled(key)) {
		return -EACCES;
	}
	LOG_DBG("Erasing %04x", key);

	/* Check if value exists */
	if (nvs_read(&fs, key, NULL, 0) == -ENOENT) {
		return -ENOENT;
	}

	/* Delete from NVS */
	rc = nvs_delete(&fs, key);
	if (rc == 0) {
		/* Notify interested parties of value deletion */
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->value_changed) {
				cb->value_changed(key, NULL, 0, cb->user_ctx);
			}
		}
	}
	return rc;
}

ssize_t kv_store_write(uint16_t key, const void *data, size_t data_len)
{
	struct kv_store_cb *cb;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_enabled(key)) {
		return -EACCES;
	}
	LOG_DBG("Writing to %04x", key);

	/* Write to NVS */
	rc = nvs_write(&fs, key, data, data_len);
	if (rc > 0) {
		/* Notify interested parties of value changes */
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->value_changed) {
				cb->value_changed(key, data, rc, cb->user_ctx);
			}
		}
	}
	return rc;
}

ssize_t kv_store_read(uint16_t key, void *data, size_t max_data_len)
{
	/* Validate key is enabled */
	if (!kv_store_key_enabled(key)) {
		return -EACCES;
	}
	LOG_DBG("Reading from %04x", key);

	/* Read from NVS */
	return nvs_read(&fs, key, data, max_data_len);
}

ssize_t kv_store_read_fallback(uint16_t key, void *data, size_t max_data_len, const void *fallback,
			       size_t fallback_len)
{
	struct kv_store_cb *cb;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_enabled(key)) {
		return -EACCES;
	}
	LOG_DBG("Read from %04x", key);

	/* Try to read key value */
	rc = nvs_read(&fs, key, data, max_data_len);
	if (rc == -ENOENT) {
		LOG_DBG("Fallback on %04x", key);
		/* Key doesn't exist, write fallback data */
		rc = nvs_write(&fs, key, fallback, fallback_len);
		if (rc == fallback_len) {
			/* Notify interested parties of value write */
			SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
				if (cb->value_changed) {
					cb->value_changed(key, fallback, fallback_len,
							  cb->user_ctx);
				}
			}
			/* Read data back out */
			rc = nvs_read(&fs, key, data, max_data_len);
		}
	}
	return rc;
}
