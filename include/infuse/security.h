/**
 * @file
 * @brief Infuse Platform Security
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Infuse platform core security module.
 * Initialises PSA and loads root cryptography keys.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_SECURITY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_SECURITY_H_

#include <psa/crypto_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse security API
 * @defgroup infuse_security_apis Infuse security APIs
 * @{
 */

/**
 * @brief Initialise core security systems
 *
 * @retval 0 on success
 * @retval -errno negative error code on failure
 */
int infuse_security_init(void);

/**
 * @brief Get device root key identifier
 *
 * @note This key is only valid for key derivation options through HKDF
 *
 * @return psa_key_id_t Device root key identifier
 */
psa_key_id_t infuse_security_device_root_key(void);

/**
 * @brief Get network root key identifier
 *
 * @note This key is only valid for key derivation options through HKDF
 *
 * @param network_id Output for Infuse network identifier
 *
 * @return psa_key_id_t Network root key identifier
 */
psa_key_id_t infuse_security_network_root_key(uint32_t *network_id);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_SECURITY_H_ */
