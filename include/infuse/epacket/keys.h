/**
 * @file
 * @brief ePacket key API
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <zephyr/sys/util.h>

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
	EPACKET_KEY_INTERFACE_BT_ADV = 2,
	EPACKET_KEY_INTERFACE_BT_GATT = 3,
	EPACKET_KEY_INTERFACE_NUM,
	EPACKET_KEY_INTERFACE_MASK = 0x7F,
};

/**
 * @brief HKDF-SHA256 based key derivation
 *
 * Derived key lifetime is `PSA_KEY_LIFETIME_VOLATILE`.
 * Derived key is only valid for `PSA_ALG_CHACHA20_POLY1305`.
 *
 * @param base_key PSA key to use as the base for derivation
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 * @param salt Key derivation randomisation
 * @param output_key_id Output key ID
 *
 * @retval 0 on success
 * @retval -EINVAL on invalid @a base_key
 * @retval -EIO on error
 */
int epacket_key_derive(psa_key_id_t base_key, const uint8_t *info, uint8_t info_len, uint32_t salt,
		       psa_key_id_t *output_key_id);

/**
 * @brief Get PSA key ID from ePacket key ID
 *
 * @param key_type ePacket key type (Combination of @ref epacket_key_type and @ref
 * epacket_key_interface)
 * @param key_identifier 3 byte key identifier
 * @param key_rotation Rotation index of ePacket key
 *
 * @return psa_key_id_t PSA key ID to use for operations, PSA_KEY_ID_NULL on error
 */
psa_key_id_t epacket_key_id_get(uint8_t key_type, uint32_t key_identifier, uint32_t key_rotation);

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
 * @brief Add another network to the key module
 *
 * @param key_id PSA key ID for the network root key
 * @param network_id Network ID associated with the PSA key
 *
 * @retval 0 On success
 * @retval -EINVAL Invalid parameters
 * @retval -EALREADY Network is already added
 * @retval -ENOMEM No more extension networks can be added
 */
int epacket_key_extension_network_add(psa_key_id_t key_id, uint32_t network_id);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_KEYS_H_ */
