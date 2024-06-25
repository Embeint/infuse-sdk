/**
 * @file
 * @brief Internal key-value store definitions
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_
#define INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	KV_FLAGS_REFLECT = BIT(0),
	KV_FLAGS_WRITE_ONLY = BIT(1),
	KV_FLAGS_READ_ONLY = BIT(2),
};

struct key_value_slot_definition {
	uint16_t key;
	uint8_t range;
	uint8_t flags;
};

/**
 * @brief Retrieve slot definitions
 *
 * @param num Number of slot definitions
 * @return struct key_value_slot_definition* Pointer to slot definitions
 */
struct key_value_slot_definition *kv_internal_slot_definitions(size_t *num);

/**
 * @brief Retrieve key metadata
 *
 * @param key Key to query
 * @param flags Flags associated with key, if key is valid, can be NULL
 * @param reflect_idx Key reflection index, SIZE_MAX if reflection disabled, can be NULL
 *
 * @return true Key is valid to use
 * @return false Key is invalid
 */
bool kv_store_key_metadata(uint16_t key, uint8_t *flags, size_t *reflect_idx);

/**
 * @brief Initialise KV reflection
 */
void kv_reflect_init(void);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_ */
