/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>

#define _KV_SLOTS_ARRAY_DEFINE kv_store_slots_internal
#include <infuse/fs/kv_types.h>
#include <infuse/fs/kv_store.h>

#include "kv_internal.h"

static K_MUTEX_DEFINE(kv_lock);
static sys_slist_t cb_list;

void kv_internal_lock(void)
{
	k_mutex_lock(&kv_lock, K_FOREVER);
}

void kv_internal_unlock(void)
{
	k_mutex_unlock(&kv_lock);
}

void kv_internal_notify(uint16_t key, const void *data, size_t data_len)
{
	struct kv_store_cb *cb, *cbs;

	/* Safe iteration allows a callback to unregister itself */
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&cb_list, cb, cbs, node) {
		if (cb->value_changed) {
			cb->value_changed(key, data, data_len, cb->user_ctx);
		}
	}
}

void kv_store_register_callback(struct kv_store_cb *cb)
{
	kv_internal_lock();
	sys_slist_append(&cb_list, &cb->node);
	kv_internal_unlock();
}

bool kv_store_unregister_callback(struct kv_store_cb *cb)
{
	bool removed;

	kv_internal_lock();
	removed = sys_slist_find_and_remove(&cb_list, &cb->node);
	kv_internal_unlock();
	return removed;
}

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
