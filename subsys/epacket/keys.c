/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
	[EPACKET_KEY_INTERFACE_BT_GATT] = "bt_gatt",
};
BUILD_ASSERT(ARRAY_SIZE(key_info) == EPACKET_KEY_INTERFACE_NUM, "");

#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE
static struct key_storage secondary_network_keys[EPACKET_KEY_INTERFACE_NUM];
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */

#if CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS > 0
struct extension_network {
	uint32_t network_id;
	psa_key_id_t base_key;
};

static struct extension_network extension_bases[CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS];
static struct key_storage extension_keys[CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS]
					[EPACKET_KEY_INTERFACE_NUM];
#endif /* CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS > 0 */

LOG_MODULE_REGISTER(epacket_keys, CONFIG_EPACKET_LOG_LEVEL);

int epacket_key_derive(psa_key_id_t base_key, const uint8_t *info, uint8_t info_len, uint32_t salt,
		       psa_key_id_t *output_key_id)
{
	if (base_key == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}
	*output_key_id = infuse_security_derive_chacha_key(base_key, &salt, sizeof(salt), info,
							   info_len, false);
	if (*output_key_id == PSA_KEY_ID_NULL) {
		return -EIO;
	}
	return 0;
}

int epacket_key_delete(psa_key_id_t key_id)
{
	return psa_destroy_key(key_id) == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t epacket_key_id_get(uint8_t key_type, uint32_t key_identifier, uint32_t key_rotation)
{
	enum epacket_key_interface interface;
	struct key_storage *storage;
	psa_key_id_t base = PSA_KEY_ID_NULL;
	const char *info;
	int64_t ticks;
	int rc;

	/* Extract key info */
	if (key_type & EPACKET_KEY_DEVICE) {
		base = infuse_security_device_root_key();
		storage = device_keys;
		if (key_identifier != infuse_security_device_key_identifier()) {
			/* Can only decode our own key */
			return PSA_KEY_ID_NULL;
		}
	} else {
		if (key_identifier == infuse_security_network_key_identifier()) {
			base = infuse_security_network_root_key();
			storage = network_keys;
		}
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE
		else if (key_identifier == infuse_security_secondary_network_key_identifier()) {
			base = infuse_security_secondary_network_root_key();
			storage = secondary_network_keys;
		}
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */
		else {
#if CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS > 0
			/* Is this an extension network? */
			for (int i = 0; i < CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS; i++) {
				if (extension_bases[i].network_id == key_identifier) {
					base = extension_bases[i].base_key;
					storage = extension_keys[i];
					break;
				}
			}

#endif /* CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS */
			if (base == PSA_KEY_ID_NULL) {
				/* Network ID not known */
				return PSA_KEY_ID_NULL;
			}
		}
	}
	interface = key_type & EPACKET_KEY_INTERFACE_MASK;
	if (interface >= EPACKET_KEY_INTERFACE_NUM) {
		return PSA_KEY_ID_NULL;
	}

	/* Handle key rotation */
	if (storage[interface].rotation != key_rotation) {
		info = key_info[interface];
		/* Delete previous derived key */
		if (storage[interface].id) {
			epacket_key_delete(storage[interface].id);
		}
		LOG_INF("Regenerating derived key %02X (%s) for rotation %d", key_type, info,
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
			return PSA_KEY_ID_NULL;
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

#if CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS > 0

int epacket_key_extension_network_add(psa_key_id_t key_id, uint32_t network_id)
{
	if (key_id == PSA_KEY_ID_NULL) {
		return -EINVAL;
	}
	for (int i = 0; i < CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS; i++) {
		if (extension_bases[i].base_key == key_id) {
			return -EALREADY;
		}
	}
	/* Find a free slot */
	for (int i = 0; i < CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS; i++) {
		if (extension_bases[i].base_key == PSA_KEY_ID_NULL) {
			/* Free slot found */
			extension_bases[i].base_key = key_id;
			extension_bases[i].network_id = network_id;
			memset(extension_keys[i], 0x00, sizeof(extension_keys[i]));
			return 0;
		}
	}
	/* No free slots */
	return -ENOMEM;
}

#endif /* CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS > 0 */
