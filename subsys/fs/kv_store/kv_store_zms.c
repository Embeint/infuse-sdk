/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/fs/kv_types.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/zms.h>

#include <infuse/fs/kv_store.h>

#include "kv_internal.h"

static struct zms_fs fs;
static sys_slist_t cb_list;

#define ZMS_PARTITION        DT_CHOSEN(infuse_kv_partition)
#define ZMS_PARTITION_ID     DT_FIXED_PARTITION_ID(ZMS_PARTITION)
#define ZMS_PARTITION_DEVICE DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(ZMS_PARTITION))
#define ZMS_PARTITION_OFFSET DT_REG_ADDR(ZMS_PARTITION)
#define ZMS_PARTITION_SIZE   DT_REG_SIZE(ZMS_PARTITION)

BUILD_ASSERT(sizeof(struct key_value_slot_definition) == 4);

LOG_MODULE_REGISTER(kv_store, CONFIG_KV_STORE_LOG_LEVEL);

void *kv_store_fs(void)
{
	return &fs;
}

int kv_store_reset(void)
{
	int rc;

	LOG_INF("Resetting KV store");
	rc = zms_clear(&fs);
	if (rc == 0) {
		rc = zms_mount(&fs);
		/* Reset the KV store reflection state */
		kv_reflect_init();
	} else {
		LOG_WRN("Failed to reset KV store (%d)", rc);
	}
	return rc;
}

void kv_store_register_callback(struct kv_store_cb *cb)
{
	sys_slist_append(&cb_list, &cb->node);
}

bool kv_store_key_exists(uint16_t key)
{
	return zms_get_data_length(&fs, ID_PRE | key) > 0;
}

ssize_t kv_store_delete(uint16_t key)
{
	struct kv_store_cb *cb;
	size_t reflect_idx;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_metadata(key, NULL, &reflect_idx)) {
		return -EACCES;
	}
	LOG_DBG("Erasing %04x", key);

	/* Check if value exists */
	if (zms_read(&fs, ID_PRE | key, NULL, 0) == -ENOENT) {
		return -ENOENT;
	}

	/* Delete from ZMS */
	rc = zms_delete(&fs, ID_PRE | key);
	if (rc == 0) {
		/* Update reflection state */
		if (reflect_idx != SIZE_MAX) {
			kv_reflect_key_updated(reflect_idx, NULL, 0);
		}
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
	size_t reflect_idx;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_metadata(key, NULL, &reflect_idx)) {
		return -EACCES;
	}

	/* Write to NVS */
	rc = zms_write(&fs, ID_PRE | key, data, data_len);
	if (rc > 0) {
		/* Update reflection state */
		if (reflect_idx != SIZE_MAX) {
			kv_reflect_key_updated(reflect_idx, data, data_len);
		}
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
	return zms_read(&fs, ID_PRE | key, data, max_data_len);
}

ssize_t kv_store_read_fallback(uint16_t key, void *data, size_t max_data_len, const void *fallback,
			       size_t fallback_len)
{
	struct kv_store_cb *cb;
	size_t reflect_idx;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_metadata(key, NULL, &reflect_idx)) {
		return -EACCES;
	}
	LOG_DBG("Read from %04x", key);

	/* Try to read key value */
	rc = zms_read(&fs, ID_PRE | key, data, max_data_len);
	if (rc == -ENOENT) {
		LOG_DBG("Fallback on %04x", key);
		/* Key doesn't exist, write fallback data */
		rc = zms_write(&fs, ID_PRE | key, fallback, fallback_len);
		if (rc == fallback_len) {
			/* Update reflection state */
			if (reflect_idx != SIZE_MAX) {
				kv_reflect_key_updated(reflect_idx, fallback, fallback_len);
			}
			/* Notify interested parties of value write */
			SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
				if (cb->value_changed) {
					cb->value_changed(key, fallback, fallback_len,
							  cb->user_ctx);
				}
			}
			/* Read data back out */
			rc = zms_read(&fs, ID_PRE | key, data, max_data_len);
		}
	}
	return rc;
}

IF_DISABLED(CONFIG_ZTEST, (static))
int kv_store_init(void)
{
	const struct flash_area *area;
	struct flash_pages_info info;
	int rc;

	fs.flash_device = ZMS_PARTITION_DEVICE;
	fs.offset = ZMS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		LOG_ERR("No page info");
		return rc;
	}
	fs.sector_size = info.size;
	fs.sector_count = ZMS_PARTITION_SIZE / info.size;

	rc = zms_mount(&fs);
	if (rc == -ENOTSUP) {
		/* Doesn't look like a filesystem, erase */
		LOG_WRN("No ZMS FS detected, resetting");
		flash_area_open(ZMS_PARTITION_ID, &area);
		flash_area_erase(area, 0, ZMS_PARTITION_SIZE);
		flash_area_close(area);
		/* Try mounting again */
		rc = zms_mount(&fs);
	}

	if (rc == 0) {
		kv_reflect_init();
	}
	return rc;
}

SYS_INIT(kv_store_init, POST_KERNEL, CONFIG_KV_STORE_INIT_PRIORITY);
