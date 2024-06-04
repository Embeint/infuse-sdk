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

#include <stdint.h>

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
 * @brief Retrieve current cloud public key
 *
 * @param public_key Storage for public key
 */
void infuse_security_cloud_public_key(uint8_t public_key[32]);

/**
 * @brief Retrieve current device public key
 *
 * @param public_key Storage for public key
 */
void infuse_security_device_public_key(uint8_t public_key[32]);

/**
 * @brief Get device root key identifier
 *
 * @note This key is only valid for key derivation options through HKDF
 *
 * @return psa_key_id_t Device root key identifier
 */
psa_key_id_t infuse_security_device_root_key(void);

/**
 * @brief Get device signing key identifier
 *
 * @note This key is only valid for ChaCha20-Poly1305 operations
 *
 * @return psa_key_id_t Device signing key identifier
 */
psa_key_id_t infuse_security_device_sign_key(void);

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
 * @brief Derive a key for use with ChaCha20-Poly1305
 *
 * @param base_key Base key to use for HKDF
 * @param salt Key derivation randomisation
 * @param salt_len Length of @a salt
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 *
 * @return psa_key_id_t Derived key identifier
 */
psa_key_id_t infuse_security_derive_chacha_key(psa_key_id_t base_key, const void *salt,
					       size_t salt_len, const void *info, size_t info_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_SECURITY_H_ */
