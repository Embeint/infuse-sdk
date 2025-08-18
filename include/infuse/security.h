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
#include <stdbool.h>

#include <zephyr/net/tls_credentials.h>

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
 * @brief Disable the Debug-Access-Port
 */
void infuse_security_disable_dap(void);

/**
 * @brief Retrieve the key attributes required for creating a key compatible with
 * @ref infuse_security_derive_chacha_key
 *
 * @return psa_key_attributes_t Key attributes
 */
psa_key_attributes_t infuse_security_hkdf_attributes(void);

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
 * @return psa_key_id_t Network root key identifier
 */
psa_key_id_t infuse_security_network_root_key(void);

/**
 * @brief Get secondary network root key identifier
 *
 * Depends on CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE.
 *
 * @note This key is only valid for key derivation options through HKDF
 *
 * @return psa_key_id_t Network root key identifier
 */
psa_key_id_t infuse_security_secondary_network_root_key(void);

/**
 * @brief Get security tag for use with Infuse-IoT COAP server
 *
 * @return sec_tag_t Security tag for use with zsock_setsockopt
 */
sec_tag_t infuse_security_coap_dtls_tag(void);

/**
 * @brief Derive a key for use with PSA
 *
 * @param base_key Base key to use for HKDF
 * @param algorithm Algorithm key will be used with
 * @param key_type Type of key to generate
 * @param key_bits Length of key to generate (bits)
 * @param key_usage How the key will be used
 * @param salt Key derivation randomisation
 * @param salt_len Length of @a salt
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 * @param force_export Force set PSA_KEY_USAGE_EXPORT attribute on generated key
 *
 * @return psa_key_id_t Derived key identifier
 */
psa_key_id_t infuse_security_derive_key(psa_key_id_t base_key, psa_algorithm_t algorithm,
					psa_key_type_t key_type, size_t key_bits,
					psa_key_usage_t key_usage, const void *salt,
					size_t salt_len, const void *info, size_t info_len,
					bool force_export);

/**
 * @brief Derive a key for use with ChaCha20-Poly1305
 *
 * @param base_key Base key to use for HKDF
 * @param salt Key derivation randomisation
 * @param salt_len Length of @a salt
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 * @param force_export Force set PSA_KEY_USAGE_EXPORT attribute on generated key
 *
 * @return psa_key_id_t Derived key identifier
 */
psa_key_id_t infuse_security_derive_chacha_key(psa_key_id_t base_key, const void *salt,
					       size_t salt_len, const void *info, size_t info_len,
					       bool force_export);

/**
 * @brief Get the current device key identifier
 *
 * The device key identifier is constructed as a CRC32 hash computed over the
 * cloud and device public keys, truncated to 24 bits.
 *
 * @return uint32_t 24bit device key identifier
 */
uint32_t infuse_security_device_key_identifier(void);

/**
 * @brief Get the current network key identifier
 *
 * @return uint32_t 24 bit network key identifier
 */
uint32_t infuse_security_network_key_identifier(void);

/**
 * @brief Get the secondary network key identifier
 *
 * Depends on CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE.
 *
 * @return uint32_t 24 bit network key identifier
 */
uint32_t infuse_security_secondary_network_key_identifier(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_SECURITY_H_ */
