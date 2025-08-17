/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/__assert.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "kv_internal.h"

#if KV_REFLECT_NUM > 0
static uint32_t value_crc_slots[KV_REFLECT_NUM];
#endif
static uint32_t reflect_crc;

LOG_MODULE_DECLARE(kv_store);

uint32_t kv_store_reflect_crc(void)
{
	return reflect_crc;
}

void kv_reflect_init(void)
{
#if KV_REFLECT_NUM > 0
	struct key_value_slot_definition *defs;
	void *fs = kv_store_fs();
	uint8_t read_buffer[256];
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
			rc = zms_read(fs, ID_PRE | key, read_buffer, sizeof(read_buffer));
#endif
			if (rc == -ENOENT) {
				/* No data, CRC = 0*/
			} else if (rc < 0) {
				/* Unexpected error */
				LOG_WRN("Unexpected error reading %d (%d)", key, rc);
			} else if (rc > sizeof(read_buffer)) {
				/* Insufficient buffer size */
				LOG_WRN("Key value %d too large for reflect (%d)", key, rc);
			} else {
				value_crc_slots[idx] = crc32_ieee(read_buffer, rc);
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
		value_crc_slots[reflect_idx] = crc32_ieee(data, len);
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
