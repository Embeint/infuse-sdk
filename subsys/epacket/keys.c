/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/security.h>
#include <infuse/epacket/keys.h>

#include <psa/crypto.h>

struct key_storage {
	uint32_t rotation;
	psa_key_id_t id;
};

static struct key_storage network_keys[EPACKET_KEY_INTERFACE_NUM];
static struct key_storage device_keys[EPACKET_KEY_INTERFACE_NUM];
static const char *const key_info[] = {
	[EPACKET_KEY_INTERFACE_SERIAL] = "serial",
	[EPACKET_KEY_INTERFACE_UDP] = "udp",
	[EPACKET_KEY_INTERFACE_BT_ADV] = "bt_adv",
};
BUILD_ASSERT(ARRAY_SIZE(key_info) == EPACKET_KEY_INTERFACE_NUM, "");

LOG_MODULE_REGISTER(epacket_keys, CONFIG_EPACKET_LOG_LEVEL);

int epacket_key_derive(enum epacket_key_type base_key, const uint8_t *info, uint8_t info_len,
		       uint32_t salt, psa_key_id_t *output_key_id)
{
	psa_key_id_t input_key;

	/* Select base key */
	switch (base_key) {
	case EPACKET_KEY_NETWORK:
		input_key = infuse_security_network_root_key();
		break;
	case EPACKET_KEY_DEVICE:
		input_key = infuse_security_device_root_key();
		break;
	default:
		return -EINVAL;
	}

	*output_key_id =
		infuse_security_derive_chacha_key(input_key, &salt, sizeof(salt), info, info_len);
	if (*output_key_id == PSA_KEY_ID_NULL) {
		return -EIO;
	}
	return 0;
}

int epacket_key_delete(psa_key_id_t key_id)
{
	return psa_destroy_key(key_id) == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t epacket_key_id_get(uint8_t key_id, uint32_t key_rotation)
{
	enum epacket_key_type base;
	enum epacket_key_interface interface;
	struct key_storage *storage;
	const char *info;
	int64_t ticks;
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
	if (storage[interface].rotation != key_rotation) {
		info = key_info[interface];
		/* Delete previous derived key */
		if (storage[interface].id) {
			epacket_key_delete(storage[interface].id);
		}
		LOG_INF("Regenerating derived key %02X (%s) for rotation %d", key_id, info,
			key_rotation);
		ticks = k_uptime_ticks();
		rc = epacket_key_derive(base, info, strlen(info), key_rotation,
					&storage[interface].id);
		ticks = k_uptime_ticks() - ticks;
		LOG_DBG("Generation took %d uS", k_cyc_to_us_near32(ticks));
		if (rc == 0) {
			storage[interface].rotation = key_rotation;
		} else {
			LOG_ERR("Key derivation failed (%d)", rc);
			return 0;
		}
	}
	return storage[interface].id;
}

#ifdef CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT

int epacket_key_export(psa_key_id_t key_id, uint8_t key[32])
{
	psa_status_t status;
	size_t olen;

	status = psa_export_key(key_id, key, 32, &olen);
	if ((status != PSA_SUCCESS) || (olen != 32)) {
		return -EINVAL;
	}
	return 0;
}

#endif /* CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT */
