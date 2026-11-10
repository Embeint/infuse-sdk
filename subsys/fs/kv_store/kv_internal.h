/**
 * @file
 * @brief Internal key-value store definitions
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_
#define INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_KV_STORE_NVS)
#define ID_PRE 0
#include <zephyr/kvss/nvs.h>
#elif defined(CONFIG_KV_STORE_ZMS)
#define ID_PRE (CONFIG_KV_STORE_ZMS_ID_PREFIX << 16)
#include <zephyr/kvss/zms.h>
#else
#error Unknown KV store backend
#endif

/**
 * @brief Lock the KV store for modification
 *
 * Serialises backend modifications, reflection updates and callback dispatch.
 * The lock is recursive, so callbacks may call back into the KV store.
 */
void kv_internal_lock(void);

/**
 * @brief Unlock the KV store
 */
void kv_internal_unlock(void);

/**
 * @brief Notify registered callbacks of a value change
 *
 * @note Must be called with the KV store lock held.
 *
 * @param key Key that changed
 * @param data Pointer to the new value, NULL on deletion
 * @param data_len Length of the new value, 0 on deletion
 */
void kv_internal_notify(uint16_t key, const void *data, size_t data_len);

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
 * @brief Validate a value about to be written to the backend
 *
 * @param key Key to be written
 * @param data_len Length of the value to be written
 * @param reflect_idx Key reflection index, SIZE_MAX if reflection disabled
 *
 * @retval 0 Write is valid
 * @retval -EACCES Key is not enabled
 * @retval -EINVAL Zero length value
 * @retval -EFBIG Reflected value larger than @kconfig{CONFIG_KV_STORE_REFLECT_MAX_SIZE}
 */
int kv_internal_write_validate(uint16_t key, size_t data_len, size_t *reflect_idx);

/**
 * @brief Initialise KV reflection
 */
void kv_reflect_init(void);

/**
 * @brief Update reflection storage with new data
 *
 * @param reflect_idx Index from @ref kv_store_key_metadata
 * @param data Pointer to updated data, NULL on deletion
 * @param len Length of updated data, 0 on deletion
 */
void kv_reflect_key_updated(size_t reflect_idx, const void *data, size_t len);

/**
 * @brief CRC value associated with a given reflection index
 *
 * @param reflect_idx Index from @ref kv_store_key_metadata
 *
 * @retval uint32_t CRC of data in index
 */
uint32_t kv_reflect_key_crc(size_t reflect_idx);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_FS_KV_INTERNAL_H_ */
