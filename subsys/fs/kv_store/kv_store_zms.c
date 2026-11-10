/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/fs/kv_types.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/kvss/zms.h>

#include <infuse/fs/kv_store.h>

#include "kv_internal.h"

static struct zms_fs fs;

#define ZMS_PARTITION        DT_CHOSEN(infuse_kv_partition)
#define ZMS_PARTITION_ID     DT_PARTITION_ID(ZMS_PARTITION)
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
	kv_internal_lock();
	rc = zms_clear(&fs);
	if (rc == 0) {
		rc = zms_mount(&fs);
		/* Reset the KV store reflection state */
		kv_reflect_init();
	} else {
		LOG_WRN("Failed to reset KV store (%d)", rc);
	}
	kv_internal_unlock();
	return rc;
}

bool kv_store_key_exists(uint16_t key)
{
	return zms_get_data_length(&fs, ID_PRE | key) > 0;
}

ssize_t kv_store_key_data_size(uint16_t key)
{
	return zms_get_data_length(&fs, ID_PRE | key);
}

ssize_t kv_store_delete(uint16_t key)
{
	size_t reflect_idx;
	ssize_t rc;

	/* Validate key is enabled */
	if (!kv_store_key_metadata(key, NULL, &reflect_idx)) {
		return -EACCES;
	}
	LOG_DBG("Erasing %04x", key);

	kv_internal_lock();
	/* Check if value exists */
	if (zms_read(&fs, ID_PRE | key, NULL, 0) == -ENOENT) {
		rc = -ENOENT;
		goto end;
	}

	/* Delete from ZMS */
	rc = zms_delete(&fs, ID_PRE | key);
	if (rc == 0) {
		/* Update reflection state */
		if (reflect_idx != SIZE_MAX) {
			kv_reflect_key_updated(reflect_idx, NULL, 0);
		}
		/* Notify interested parties of value deletion */
		kv_internal_notify(key, NULL, 0);
	}
end:
	kv_internal_unlock();
	return rc;
}

ssize_t kv_store_write(uint16_t key, const void *data, size_t data_len)
{
	size_t reflect_idx;
	ssize_t rc;

	rc = kv_internal_write_validate(key, data_len, &reflect_idx);
	if (rc < 0) {
		return rc;
	}
	LOG_DBG("Writing to %04x", key);

	kv_internal_lock();
	/* Write to ZMS */
	rc = zms_write(&fs, ID_PRE | key, data, data_len);
	if (rc > 0) {
		/* Update reflection state */
		if (reflect_idx != SIZE_MAX) {
			kv_reflect_key_updated(reflect_idx, data, data_len);
		}
		/* Notify interested parties of value changes */
		kv_internal_notify(key, data, rc);
	}
	kv_internal_unlock();
	return rc;
}

static ssize_t backend_read(uint16_t key, void *data, size_t max_data_len)
{
	/* zms_read() truncates the return value to max_data_len, while the KV store
	 * API returns the full stored length (matching NVS). Read the latest entry
	 * through zms_read_hist() to get that length.
	 */
	return zms_read_hist(&fs, ID_PRE | key, data, max_data_len, 0);
}

ssize_t kv_store_read(uint16_t key, void *data, size_t max_data_len)
{
	/* Validate key is enabled */
	if (!kv_store_key_enabled(key)) {
		return -EACCES;
	}
	LOG_DBG("Reading from %04x", key);

	/* Read from ZMS */
	return backend_read(key, data, max_data_len);
}

ssize_t kv_store_read_fallback(uint16_t key, void *data, size_t max_data_len, const void *fallback,
			       size_t fallback_len)
{
	size_t reflect_idx;
	ssize_t rc;

	/* The fallback value may be written to the backend */
	rc = kv_internal_write_validate(key, fallback_len, &reflect_idx);
	if (rc < 0) {
		return rc;
	}
	LOG_DBG("Read from %04x", key);

	/* Hold the lock so a concurrent write is not overwritten by the fallback */
	kv_internal_lock();
	/* Try to read key value */
	rc = backend_read(key, data, max_data_len);
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
			kv_internal_notify(key, fallback, fallback_len);
			/* Read data back out */
			rc = backend_read(key, data, max_data_len);
		}
	}
	kv_internal_unlock();
	return rc;
}

IF_DISABLED(CONFIG_ZTEST, (static))
int kv_store_init(void)
{
	const struct flash_area *area;
	struct flash_pages_info info;
	int rc;

	kv_internal_lock();
	fs.flash_device = ZMS_PARTITION_DEVICE;
	fs.offset = ZMS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		LOG_ERR("No page info");
		goto end;
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
end:
	kv_internal_unlock();
	return rc;
}

SYS_INIT(kv_store_init, POST_KERNEL, CONFIG_KV_STORE_INIT_PRIORITY);
