/**
 * @file
 * @brief PSA Internal-Trusted-Storage implementation
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_FS_SECURE_STORAGE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_FS_SECURE_STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief secure_storage API
 * @defgroup secure_storage_apis secure_storage APIs
 * @{
 */

/**
 * @brief Initialise secure storage
 *
 * @note Requires psa_crypto_init to have been run
 *
 * @retval 0 on success
 * @retval -errno negative error code otherwise
 */
int secure_storage_init(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_FS_SECURE_STORAGE_H_ */
