/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <eis/crypto/ascon.h>
#include <eis/epacket/keys.h>

#include "mbedtls/hkdf.h"

/* Hardcoded for initial dev */
static const uint8_t network_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const uint8_t device_key[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				       0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

struct key_storage {
	uint32_t rotation;
	uint8_t key[16];
};

static struct key_storage network_keys[EPACKET_KEY_INTERFACE_NUM];
static struct key_storage device_keys[EPACKET_KEY_INTERFACE_NUM];
static const char *const key_info[] = {
	[EPACKET_KEY_INTERFACE_SERIAL] = "serial",
	[EPACKET_KEY_INTERFACE_UDP] = "udp",
};
BUILD_ASSERT(ARRAY_SIZE(key_info) == EPACKET_KEY_INTERFACE_NUM, "");

LOG_MODULE_REGISTER(epacket_keys, CONFIG_EPACKET_LOG_LEVEL);

int epacket_key_derive(enum epacket_key_type base_key, uint8_t *output_key, uint8_t output_key_len, const uint8_t *info,
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

static uint8_t *key_get(uint8_t key_id, uint32_t key_rotation)
{
	enum epacket_key_type base;
	enum epacket_key_interface interface;
	struct key_storage *storage;
	const char *info;
	int64_t ticks;
	uint8_t *key;
	int rc;

	/* Extract key info */
	if (key_id & EPACKET_KEY_DEVICE) {
		base = EPACKET_KEY_DEVICE;
		storage = device_keys;
	} else {
		base = EPACKET_KEY_NETWORK;
		storage = network_keys;
	}
	interface = key_id & EPACKET_KEY_INTERFACE_MASK;

	/* Handle key rotation */
	key = storage[interface].key;
	if (storage[interface].rotation != key_rotation) {
		info = key_info[interface];
		LOG_INF("Regenerating derived key %02X (%s) for rotation %d", key_id, info, key_rotation);
		ticks = k_uptime_ticks();
		rc = epacket_key_derive(base, key, 16, info, strlen(info), key_rotation);
		ticks = k_uptime_ticks() - ticks;
		LOG_DBG("Generation took %d uS", k_cyc_to_us_near32(ticks));
		if (rc == 0) {
			storage[interface].rotation = key_rotation;
		} else {
			LOG_ERR("Key derivation failed (%d)", rc);
			return NULL;
		}
	}
	return key;
}

int epacket_encrypt(uint8_t key_id, uint32_t key_rotation, const uint8_t *associated_data, uint32_t associated_data_len,
		    const uint8_t *plaintext, uint32_t plaintext_len, const uint8_t *nonce, uint8_t *tag,
		    uint8_t *ciphertext)
{
	unsigned long long clen;
	uint8_t *key;

	key = key_get(key_id, key_rotation);
	if (key == NULL) {
		return -1;
	}

	/* Encrypt payload */
	return ascon128a_aead_encrypt(ciphertext, &clen, plaintext, plaintext_len, associated_data, associated_data_len,
				      tag, nonce, key);
}

int epacket_decrypt(uint8_t key_id, uint32_t key_rotation, const uint8_t *associated_data, uint32_t associated_data_len,
		    const uint8_t *ciphertext, uint32_t ciphertext_len, const uint8_t *nonce, const uint8_t *tag,
		    uint8_t *plaintext)
{
	unsigned long long plen;
	uint8_t *key;

	key = key_get(key_id, key_rotation);
	if (key == NULL) {
		return -1;
	}

	/* Decrypt payload */
	return ascon128a_aead_decrypt(plaintext, &plen, tag, ciphertext, ciphertext_len, associated_data,
				      associated_data_len, nonce, key);
}
