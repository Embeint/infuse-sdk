/**
 * @file
 * @brief Hardware Unique Key API
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_HARDWARE_UNIQUE_KEY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_HARDWARE_UNIQUE_KEY_H_

#include <stdint.h>
#include <psa/crypto.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief hardware_unique_key API
 * @defgroup hardware_unique_key_apis hardware_unique_key APIs
 * @{
 */

/**
 * @brief Initialise Hardware Unique Key
 *
 * @retval 0 on success
 * @retval -errno error code on failure
 */
int hardware_unique_key_init(void);

/**
 * @brief Get Hardware Unique Key ID
 *
 * @return psa_key_id_t PSA key identifier
 */
psa_key_id_t hardware_unique_key_id(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_CRYPTO_HARDWARE_UNIQUE_KEY_H_ */
