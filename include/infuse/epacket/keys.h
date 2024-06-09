/**
 * @file
 * @brief ePacket key API
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <psa/crypto.h>

/**
 * @brief ePacket key API
 * @defgroup epacket_key_apis ePacket key APIs
 * @{
 */

enum epacket_key_type {
	EPACKET_KEY_NETWORK = 0,
	EPACKET_KEY_DEVICE = BIT(7),
};

enum epacket_key_interface {
	EPACKET_KEY_INTERFACE_SERIAL = 0,
	EPACKET_KEY_INTERFACE_UDP = 1,
	EPACKET_KEY_INTERFACE_NUM,
	EPACKET_KEY_INTERFACE_MASK = 0x7F,
};

/**
 * @brief HKDF-SHA256 based key derivation
 *
 * Derived key lifetime is `PSA_KEY_LIFETIME_VOLATILE`.
 * Derived key is only valid for `PSA_ALG_CHACHA20_POLY1305`.
 *
 * @param base_key Derive from network key or device key
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 * @param salt Key derivation randomisation
 * @param output_key_id Output key ID
 *
 * @retval 0 on success
 * @retval -EINVAL on invalid @a base_key
 * @retval -EIO on error
 */
int epacket_key_derive(enum epacket_key_type base_key, const uint8_t *info, uint8_t info_len,
		       uint32_t salt, psa_key_id_t *output_key_id);

/**
 * @brief Get PSA key ID from ePacket key ID
 *
 * @param key_id ePacket key ID
 * @param key_rotation Rotation index of ePacket key
 *
 * @return psa_key_id_t PSA key ID to use for operations, 0 on error
 */
psa_key_id_t epacket_key_id_get(uint8_t key_id, uint32_t key_rotation);

/**
 * @brief Delete a PSA key ID
 *
 * @param key_id PSA key ID
 * @retval 0 on success
 * @retval -EINVAL on invalid key
 */
int epacket_key_delete(psa_key_id_t key_id);

#ifdef CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT

/**
 * @brief Export ePacket key for test purposes
 *
 * @param key_id PSA key ID
 * @param key Storage for key
 *
 * @retval 0 on success
 * @retval -EINVAL on invalid key
 */
int epacket_key_export(psa_key_id_t key_id, uint8_t key[32]);

#endif /* CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_ */
