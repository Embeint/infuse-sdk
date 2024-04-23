/**
 * @file
 * @brief Typed key-value store for Infuse IoT
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Built on top of Zephyr NVS with the addition of defined types per key
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_FS_KV_STORE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_FS_KV_STORE_H_

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief kv_store API
 * @defgroup kv_store_apis kv_store APIs
 * @{
 */

/**
 * @brief Initialise key-value storage
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int kv_store_init(void);

/**
 * @brief Reset key-value storage
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int kv_store_reset(void);

/**
 * @brief Check whether a given key is valid for reading/writing
 *
 * @param key Key to check
 *
 * @return true Key is valid to use
 * @return false Key is invalid
 */
bool kv_store_key_enabled(uint16_t key);

/**
 * @brief Delete a value from the KV store
 *
 * @param key Key to delete
 *
 * @retval 0 if key was deleted
 * @retval -EACCES if key is not enabled
 * @retval -ENOENT if key does not exist
 */
ssize_t kv_store_delete(uint16_t key);

/**
 * @brief Write a value to the KV store
 *
 * @param key Key to write to
 * @param data Pointer to value
 * @param data_len Length of value in bytes
 *
 * @retval 0 if data already matched
 * @retval >0 length of data written
 * @retval -EACCES if key is not enabled
 * @retval -errno value from @a nvs_write otherwise
 */
ssize_t kv_store_write(uint16_t key, const void *data, size_t data_len);

/**
 * @brief Write a constant length key to the KV store
 *
 * @param key Key to write to
 * @param data Pointer to value
 *
 * @return a value from @a kv_store_write
 */
#define KV_STORE_WRITE(key, data) kv_store_write(key, data, _##key##_SIZE)

/**
 * @brief Read a value from the KV store
 *
 * @param key Key to read from
 * @param data Pointer to buffer for data
 * @param max_data_len Maximum size of data buffer
 *
 * @retval >0 length of data read
 * @retval -EACCES if key is not enabled
 * @retval -errno value from @a nvs_read otherwise
 */
ssize_t kv_store_read(uint16_t key, void *data, size_t max_data_len);

/**
 * @brief Read a constant length key from the KV store
 *
 * @param key Key to read from
 * @param data Pointer to value
 *
 * @return a value from @a kv_store_read
 */
#define KV_STORE_READ(key, data) kv_store_read(key, data, _##key##_SIZE)

/**
 * @brief Read a key from the KV store, with a fallback if it doesn't exist
 *
 * @param key Key to read from
 * @param data Pointer to buffer for data
 * @param max_data_len Maximum size of data buffer
 * @param fallback Pointer to the fallback value
 * @param fallback_len Size of the fallback value
 *
 * @return ssize_t
 */
ssize_t kv_store_read_fallback(uint16_t key, void *data, size_t max_data_len, const void *fallback,
			       size_t fallback_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_FS_KV_STORE_H_ */
