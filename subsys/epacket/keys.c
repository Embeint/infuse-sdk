/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <errno.h>

#include <eis/epacket/keys.h>

#include "mbedtls/hkdf.h"

/* Hardcoded for initial dev */
static const uint8_t network_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const uint8_t device_key[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				       0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

int epacket_key_derive(enum epacket_base_key base_key, uint8_t *output_key, uint8_t output_key_len, const uint8_t *info,
		       uint8_t info_len, uint32_t salt)
{
	const uint8_t *input_key;

	/* Select base key */
	switch (base_key) {
	case EPACKET_KEY_NETWORK:
		input_key = network_key;
		break;
	case EPACKET_KEY_DEVICE:
		input_key = device_key;
		break;
	default:
		return -EINVAL;
	}

	/* Derive new key */
	return mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (uint8_t *)&salt, sizeof(salt), input_key, 16,
			    info, info_len, output_key, output_key_len);
}
