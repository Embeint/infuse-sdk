/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef KV_STORE_USER_KEYS_ARRAY_DEFINE

#include <infuse/fs/kv_store.h>

#include "infuse_kv_user_types.h"

struct key_value_slot_definition users_defs[] = {
#endif
	{
		.key = KV_KEY_USER_1,
		.range = 1,
		.flags = KV_FLAGS_REFLECT | KV_FLAGS_READ_ONLY,
	},
	{
		.key = KV_KEY_USER_2,
		.range = 1,
		.flags = KV_FLAGS_REFLECT | KV_FLAGS_WRITE_ONLY,
	},
#ifndef KV_STORE_USER_KEYS_ARRAY_DEFINE
};
#endif
