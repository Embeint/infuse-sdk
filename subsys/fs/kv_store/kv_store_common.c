/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>
#include <errno.h>

#include <zephyr/sys/util.h>

#define _KV_SLOTS_ARRAY_DEFINE kv_store_slots_internal
#include <infuse/fs/kv_types.h>

struct key_value_slot_definition *kv_internal_slot_definitions(size_t *num)
{
	*num = ARRAY_SIZE(_KV_SLOTS_ARRAY_DEFINE);
	return _KV_SLOTS_ARRAY_DEFINE;
}

bool kv_store_key_metadata(uint16_t key, uint8_t *flags, size_t *reflect_idx)
{
	struct key_value_slot_definition *defs;
	size_t idx = 0;
	size_t num;

	defs = kv_internal_slot_definitions(&num);
	for (size_t i = 0; i < num; i++) {
		if (IN_RANGE(key, defs[i].key, defs[i].key + defs[i].range - 1)) {
			if (flags != NULL) {
				*flags = defs[i].flags;
			}
			if (reflect_idx != NULL) {
				*reflect_idx = (defs[i].flags & KV_FLAGS_REFLECT)
						       ? idx + (key - defs[i].key)
						       : SIZE_MAX;
			}
			return true;
		}
		if (defs[i].flags & KV_FLAGS_REFLECT) {
			idx += defs[i].range;
		}
	}
	return false;
}

bool kv_store_key_enabled(uint16_t key)
{
	return kv_store_key_metadata(key, NULL, NULL);
}

int kv_store_external_write_only(uint16_t key)
{
	uint8_t flags;

	if (!kv_store_key_metadata(key, &flags, NULL)) {
		return -EACCES;
	}
	/* If flag is set, operation not permitted */
	return flags & KV_FLAGS_WRITE_ONLY ? -EPERM : 0;
}

int kv_store_external_read_only(uint16_t key)
{
	uint8_t flags;

	if (!kv_store_key_metadata(key, &flags, NULL)) {
		return -EACCES;
	}
	/* If flag is set, operation not permitted */
	return flags & KV_FLAGS_READ_ONLY ? -EPERM : 0;
}
