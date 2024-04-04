/**
 * @file
 * @brief ePacket key API
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef EMBEINT_SDK_INCLUDE_EIS_EPACKET_KEYS_H_
#define EMBEINT_SDK_INCLUDE_EIS_EPACKET_KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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
 * @param base_key Derive from network key or device key
 * @param output_key Output key storage
 * @param output_key_len Length of @a output_key
 * @param info Optional application/usage specific array
 * @param info_len Length of @a info
 * @param salt Key derivation randomisation
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int epacket_key_derive(enum epacket_key_type base_key, uint8_t *output_key, uint8_t output_key_len, const uint8_t *info,
		       uint8_t info_len, uint32_t salt);

/**
 * @brief Encrypt an ePacket payload
 *
 * @param key_id Derived key to encrypt payload with
 * @param key_rotation Rotation index of derived key
 * @param associated_data Associated data array
 * @param associated_data_len Length of associated data
 * @param plaintext Input plaintext
 * @param plaintext_len Length of plaintext
 * @param nonce 16 byte nonce (Initialisation vector)
 * @param tag 16 byte ciphertext tag
 * @param ciphertext Output ciphertext of length @a plaintext_len
 *
 * @retval 0 on success
 * @retval -1 on error
 */
int epacket_encrypt(uint8_t key_id, uint32_t key_rotation, const uint8_t *associated_data, uint32_t associated_data_len,
		    const uint8_t *plaintext, uint32_t plaintext_len, const uint8_t *nonce, uint8_t *tag,
		    uint8_t *ciphertext);

/**
 * @brief Decrypt an ePacket payload
 *
 * @param key_id Derived key to decrypt payload with
 * @param key_rotation Rotation index of derived key
 * @param associated_data Associated data array
 * @param associated_data_len Length of associated data
 * @param ciphertext Input ciphertext
 * @param ciphertext_len Length of ciphertext
 * @param nonce 16 byte nonce (Initialisation vector)
 * @param tag 16 byte ciphertext tag
 * @param plaintext Output plaintext of length @a message_len
 */
int epacket_decrypt(uint8_t key_id, uint32_t key_rotation, const uint8_t *associated_data, uint32_t associated_data_len,
		    const uint8_t *ciphertext, uint32_t ciphertext_len, const uint8_t *nonce, const uint8_t *tag,
		    uint8_t *plaintext);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_KEYS_H_ */
