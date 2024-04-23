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

#include <infuse/epacket/keys.h>

#include <psa/crypto.h>

/* Hardcoded for initial dev */
static uint32_t network_id = 0x123456;
static const uint8_t network_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const uint8_t device_key[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				       0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

struct key_storage {
	uint32_t rotation;
	psa_key_id_t id;
};

static psa_key_id_t network_root_key_id;
static psa_key_id_t device_root_key_id;

static struct key_storage network_keys[EPACKET_KEY_INTERFACE_NUM];
static struct key_storage device_keys[EPACKET_KEY_INTERFACE_NUM];
static const char *const key_info[] = {
	[EPACKET_KEY_INTERFACE_SERIAL] = "serial",
	[EPACKET_KEY_INTERFACE_UDP] = "udp",
};
BUILD_ASSERT(ARRAY_SIZE(key_info) == EPACKET_KEY_INTERFACE_NUM, "");

LOG_MODULE_REGISTER(epacket_keys, CONFIG_EPACKET_LOG_LEVEL);

static int epacket_import_root_keys(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	/* Configure the input key attributes */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 128);

	/* Import the base keys into the keystore */
	status = psa_import_key(&key_attributes, device_key, sizeof(device_key), &device_root_key_id);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "device", status);
	}
	status = psa_import_key(&key_attributes, network_key, sizeof(network_key), &network_root_key_id);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "root", status);
	}
	return 0;
}

uint32_t epacket_network_key_id(void)
{
	return network_id;
}

int epacket_key_derive(enum epacket_key_type base_key, const uint8_t *info, uint8_t info_len, uint32_t salt,
		       psa_key_id_t *output_key_id)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
	psa_key_id_t input_key;
	psa_status_t status;
	static bool inited;

	/* Import base keys to PSA */
	if (!inited) {
		/* TODO: init in common application code */
		status = psa_crypto_init();
		if (status != PSA_SUCCESS) {
			LOG_ERR("PSA init failed! (%d)", status);
		}
		epacket_import_root_keys();
		inited = true;
	}

	/* Select base key */
	switch (base_key) {
	case EPACKET_KEY_NETWORK:
		input_key = network_root_key_id;
		break;
	case EPACKET_KEY_DEVICE:
		input_key = device_root_key_id;
		break;
	default:
		return -EINVAL;
	}

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_CHACHA20_POLY1305);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&key_attributes, 256);
#ifdef CONFIG_EPACKET_KEY_EXPORT
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_EXPORT);
#endif /* CONFIG_EPACKET_KEY_EXPORT */

	status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	if (status != PSA_SUCCESS) {
		return -EIO;
	}
	status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, (uint8_t *)&salt,
						sizeof(salt));
	if (status != PSA_SUCCESS) {
		return -EIO;
	}
	status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO, info, info_len);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}
	status = psa_key_derivation_input_key(&operation, PSA_KEY_DERIVATION_INPUT_SECRET, input_key);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}
	status = psa_key_derivation_output_key(&key_attributes, &operation, output_key_id);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}
	status = psa_key_derivation_abort(&operation);
	if (status != PSA_SUCCESS) {
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
		LOG_INF("Regenerating derived key %02X (%s) for rotation %d", key_id, info, key_rotation);
		ticks = k_uptime_ticks();
		rc = epacket_key_derive(base, info, strlen(info), key_rotation, &storage[interface].id);
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

#ifdef CONFIG_EPACKET_KEY_EXPORT

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

#endif /* CONFIG_EPACKET_KEY_EXPORT */
