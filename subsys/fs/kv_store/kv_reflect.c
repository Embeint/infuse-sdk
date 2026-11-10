/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "kv_internal.h"

#if KV_REFLECT_NUM > 0
static uint32_t value_crc_slots[KV_REFLECT_NUM];
/* Static to keep it off the init stack, only used with the KV store lock held */
static uint8_t read_buffer[CONFIG_KV_STORE_REFLECT_MAX_SIZE];
#endif
static uint32_t reflect_crc;

LOG_MODULE_DECLARE(kv_store, CONFIG_KV_STORE_LOG_LEVEL);

uint32_t kv_store_reflect_crc(void)
{
	return reflect_crc;
}

#if KV_REFLECT_NUM > 0
static uint32_t value_crc(const void *data, size_t len)
{
	if (len > CONFIG_KV_STORE_REFLECT_MAX_SIZE) {
		/* Oversized values can only exist if they were written before the size
		 * limit was enforced (or with a larger limit). They can't be read back
		 * at boot, so CRC the length to keep the slot CRC stable across reboots.
		 */
		uint32_t len_le = sys_cpu_to_le32(len);

		return crc32_ieee((const void *)&len_le, sizeof(len_le));
	}
	return crc32_ieee(data, len);
}
#endif /* KV_REFLECT_NUM > 0 */

void kv_reflect_init(void)
{
#if KV_REFLECT_NUM > 0
	struct key_value_slot_definition *defs;
	void *fs = kv_store_fs();
	uint16_t key, idx = 0;
	size_t num;
	int rc;

	/* Individual slot CRCs */
	defs = kv_internal_slot_definitions(&num);
	for (int i = 0; i < num; i++) {
		/* Ignore slots without the reflect flag */
		if (!(defs[i].flags & KV_FLAGS_REFLECT)) {
			continue;
		}
		key = defs[i].key;
		for (int j = 0; j < defs[i].range; j++) {
			__ASSERT_NO_MSG(idx < ARRAY_SIZE(value_crc_slots));
			value_crc_slots[idx] = 0x00;
			/* Read contents */
#if defined(CONFIG_KV_STORE_NVS)
			rc = nvs_read(fs, key, read_buffer, sizeof(read_buffer));
#elif defined(CONFIG_KV_STORE_ZMS)
			/* Full stored length is required to detect oversized values */
			rc = zms_read_hist(fs, ID_PRE | key, read_buffer, sizeof(read_buffer), 0);
#endif
			if (rc == -ENOENT) {
				/* No data, CRC = 0*/
			} else if (rc < 0) {
				/* Unexpected error */
				LOG_WRN("Unexpected error reading %d (%d)", key, rc);
			} else {
				if (rc > sizeof(read_buffer)) {
					LOG_WRN("Key value %d too large for reflect (%d)", key, rc);
				}
				value_crc_slots[idx] = value_crc(read_buffer, rc);
			}
			idx++;
			key++;
		}
	}

	/* Global CRC */
	reflect_crc = crc32_ieee((void *)value_crc_slots, sizeof(value_crc_slots));
#endif
}

void kv_reflect_key_updated(size_t reflect_idx, const void *data, size_t len)
{
#if KV_REFLECT_NUM > 0
	__ASSERT_NO_MSG(reflect_idx < ARRAY_SIZE(value_crc_slots));
	/* Update slot value */
	if (data == NULL) {
		value_crc_slots[reflect_idx] = 0x00;
	} else {
		value_crc_slots[reflect_idx] = value_crc(data, len);
	}
	/* Recalculate global CRC */
	reflect_crc = crc32_ieee((void *)value_crc_slots, sizeof(value_crc_slots));
#endif
}

uint32_t kv_reflect_key_crc(size_t reflect_idx)
{
#if KV_REFLECT_NUM > 0
	__ASSERT_NO_MSG(reflect_idx < ARRAY_SIZE(value_crc_slots));
	return value_crc_slots[reflect_idx];
#else
	return 0x00;
#endif
}
