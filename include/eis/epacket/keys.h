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

enum epacket_base_key {
	EPACKET_KEY_NETWORK,
	EPACKET_KEY_DEVICE,
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
int epacket_key_derive(enum epacket_base_key base_key, uint8_t *output_key, uint8_t output_key_len, const uint8_t *info,
		       uint8_t info_len, uint32_t salt);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_KEYS_H_ */
