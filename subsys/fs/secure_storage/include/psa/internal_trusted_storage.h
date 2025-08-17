/**
 * @file
 * @brief PSA internal trusted storage functions
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_FS_SECURE_STORAGE_INCLUDE_PSA_INTERNAL_TRUSTED_STORAGE_H_
#define INFUSE_SDK_SUBSYS_FS_SECURE_STORAGE_INCLUDE_PSA_INTERNAL_TRUSTED_STORAGE_H_

#include <stdint.h>
#include <stdlib.h>

#include <infuse/fs/kv_types.h>

#include <psa/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief psa_internal_trusted_storage PSA ITS API
 * @defgroup psa_internal_trusted_storage_apis psa_internal_trusted_storage APIs
 * @{
 */

#define PSA_KEY_ID_INFUSE_MIN KV_KEY_SECURE_STORAGE_RESERVED
#define PSA_KEY_ID_INFUSE_MAX KV_KEY_SECURE_STORAGE_RESERVED_MAX

/** @brief Flags used when creating a data entry
 */
typedef uint32_t psa_storage_create_flags_t;

/** @brief A type for UIDs used for identifying data
 */
typedef uint64_t psa_storage_uid_t;

enum psa_storage_flags {
	/* No flags to pass */
	PSA_STORAGE_FLAG_NONE = 0,
	/* The data associated with the uid will not be able to be modified or deleted. Intended to
	 * be used to set bits in `psa_storage_create_flags_t`
	 */
	PSA_STORAGE_FLAG_WRITE_ONCE = (1 << 0),
};

/**
 * @brief A container for metadata associated with a specific uid
 */
struct psa_storage_info_t {
	/* The size of the data associated with a uid */
	uint32_t size;
	/* The flags set when the uid was created */
	psa_storage_create_flags_t flags;
};

/**
 * @brief create a new or modify an existing uid/value pair
 *
 * @param uid The identifier for the data
 * @param data_length The size in bytes of the data in `p_data`
 * @param p_data A buffer containing the data
 * @param create_flags The flags that the data will be stored with
 *
 * @retval PSA_SUCCESS                     The operation completed successfully
 * @retval PSA_ERROR_NOT_PERMITTED         The operation failed because the provided `uid` value was
 * already created with PSA_STORAGE_FLAG_WRITE_ONCE
 * @retval PSA_ERROR_NOT_SUPPORTED The operation failed because one or more of the flags provided in
 * `create_flags` is not supported or is not valid
 * @retval PSA_ERROR_INSUFFICIENT_STORAGE  The operation failed because there was insufficient space
 * on the storage medium
 * @retval PSA_ERROR_STORAGE_FAILURE       The operation failed because the physical storage has
 * failed (Fatal error)
 * @retval PSA_ERROR_INVALID_ARGUMENT      The operation failed because one of the provided
 * pointers(`p_data`) is invalid, for example is `NULL` or references memory the caller cannot
 * access
 */
psa_status_t psa_its_set(psa_storage_uid_t uid, uint32_t data_length, const void *p_data,
			 psa_storage_create_flags_t create_flags);

/**
 * @brief Retrieve the value associated with a provided uid
 *
 * @param uid The uid value
 * @param data_offset The starting offset of the data requested
 * @param data_length The amount of data requested (and the minimum allocated size of the
 * `p_data` buffer)
 * @param p_data The buffer where the data will be placed upon successful completion
 * @param p_data_length The amount of data returned in the p_data buffer
 *
 * @retval PSA_SUCCESS The operation completed successfully
 * @retval PSA_ERROR_DOES_NOT_EXIST The operation failed because the provided `uid` value
 * was not found in the storage
 * @retval PSA_ERROR_STORAGE_FAILURE The operation failed because the physical storage has
 * failed (Fatal error)
 * @retval PSA_ERROR_DATA_CORRUPT The operation failed because stored data has been
 * corrupted
 * @retval PSA_ERROR_INVALID_ARGUMENT
 * The operation failed because one of the provided pointers(`p_data`, `p_data_length`) is
 * invalid. For example is `NULL` or references memory the caller cannot access. In
 * addition, this can also happen if an invalid offset was provided.
 */
psa_status_t psa_its_get(psa_storage_uid_t uid, uint32_t data_offset, uint32_t data_length,
			 void *p_data, size_t *p_data_length);

/**
 * @brief Retrieve the metadata about the provided uid
 *
 * @param uid The uid value
 * @param p_info A pointer to the `psa_storage_info_t` struct that will be populated with
 * the metadata
 *
 * @retval PSA_SUCCESS he operation completed successfully
 * @retval PSA_ERROR_DOES_NOT_EXIST The operation failed because the provided uid value was
 * not found in the storage
 * @retval PSA_ERROR_DATA_CORRUPT The operation failed because stored data has been
 * corrupted
 * @retval PSA_ERROR_INVALID_ARGUMENT  The operation failed because one of the provided
 * pointers(`p_info`) is invalid, for example is `NULL` or references memory the caller
 * cannot access
 */
psa_status_t psa_its_get_info(psa_storage_uid_t uid, struct psa_storage_info_t *p_info);

/**
 * @brief Remove the provided key and its associated data from the storage
 *
 * @param uid   The uid value
 *
 * @retval PSA_SUCCESS The operation completed successfully
 * @retval PSA_ERROR_DOES_NOT_EXIST The operation failed because the provided key value was
 * not found in the storage
 * @retval PSA_ERROR_NOT_PERMITTED The operation failed because the provided key value was
 * created with PSA_STORAGE_FLAG_WRITE_ONCE
 * @retval PSA_ERROR_STORAGE_FAILURE The operation failed because the physical storage has
 * failed (Fatal error)
 */
psa_status_t psa_its_remove(psa_storage_uid_t uid);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_FS_SECURE_STORAGE_INCLUDE_PSA_INTERNAL_TRUSTED_STORAGE_H_ */
