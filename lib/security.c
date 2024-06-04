/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

#include <infuse/security.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/secure_storage.h>

#include <psa/crypto.h>
#ifdef CONFIG_INFUSE_SECURE_STORAGE
#include <psa/internal_trusted_storage.h>
#endif

enum {
	INFUSE_ROOT_ECC_KEY_ID = KV_KEY_SECURE_STORAGE_RESERVED,
	INFUSE_ROOT_NETWORK_KEY_ID,
};

static const uint8_t infuse_cloud_public_key[] = {
	0xc2, 0xfc, 0x16, 0x76, 0xa5, 0xda, 0xf5, 0x38, 0x8e, 0x64, 0x26,
	0x99, 0x83, 0xbf, 0xa6, 0x28, 0xfd, 0x9b, 0xf0, 0x94, 0xca, 0x51,
	0x58, 0x78, 0xec, 0x8f, 0xdb, 0xdb, 0x94, 0xb6, 0x3b, 0x44,
};
static const uint8_t default_network_key[32] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
	0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static psa_key_id_t device_root_key, network_root_key;
static uint32_t cached_network_id;

LOG_MODULE_REGISTER(security, LOG_LEVEL_INF);

static psa_key_attributes_t hkdf_derive_attributes(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 256);

	return key_attributes;
}

#ifdef CONFIG_INFUSE_SECURE_STORAGE
static psa_key_id_t root_ecc_key_id;

static psa_key_id_t generate_root_ecc_key_pair(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;
	psa_key_id_t key_id;

	/* ECDH, Curve25519 */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDH);
	psa_set_key_bits(&key_attributes, 255);

	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
	psa_set_key_id(&key_attributes, INFUSE_ROOT_ECC_KEY_ID);

	status = psa_generate_key(&key_attributes, &key_id);
	if (status == PSA_ERROR_ALREADY_EXISTS) {
		LOG_DBG("Root ECC key already exists");
		key_id = INFUSE_ROOT_ECC_KEY_ID;
	} else if (status != PSA_SUCCESS) {
		key_id = PSA_KEY_ID_NULL;
	}
	return key_id;
}

static psa_key_id_t derive_shared_secret(psa_key_id_t root_key_id)
{
	psa_key_attributes_t key_attributes = hkdf_derive_attributes();
	uint8_t shared_secret[32];
	psa_status_t status;
	psa_key_id_t key_id;
	size_t olen;

	/* Calculate shared secret */
	status = psa_raw_key_agreement(PSA_ALG_ECDH, root_key_id, infuse_cloud_public_key,
				       sizeof(infuse_cloud_public_key), shared_secret,
				       sizeof(shared_secret), &olen);
	if (status != PSA_SUCCESS) {
		return PSA_KEY_ID_NULL;
	}

	/* Import device shared key into PSA */
	status = psa_import_key(&key_attributes, shared_secret, sizeof(shared_secret), &key_id);

	/* Clear sensitive stack content */
	mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));

	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "device", status);
		return PSA_KEY_ID_NULL;
	}
	return key_id;
}
#endif /* CONFIG_INFUSE_SECURE_STORAGE */

static psa_key_id_t network_key_load(void)
{
	psa_key_attributes_t key_attributes = hkdf_derive_attributes();
	psa_status_t status;
	psa_key_id_t key_id;

	/* Always default network key for now */
	status = psa_import_key(&key_attributes, default_network_key, sizeof(default_network_key),
				&key_id);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "root", status);
		return PSA_KEY_ID_NULL;
	}

	return key_id;
}

int infuse_security_init(void)
{
	psa_status_t status;

	/* Initialise crypto system */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_ERR("PSA init failed! (%d)", status);
		return -EINVAL;
	}

#ifdef CONFIG_INFUSE_SECURE_STORAGE
	int rc;

	/* Initialise secure storage  */
	rc = secure_storage_init();
	if (rc < 0) {
		LOG_ERR("Failed to init secure storage! (%d)", rc);
		return -EINVAL;
	}
	/* Create/import device root ECC key pair */
	root_ecc_key_id = generate_root_ecc_key_pair();
	if (root_ecc_key_id == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to generate root key pair! (%d)", status);
		return -EINVAL;
	}
	/* Regenerate root shared secret */
	device_root_key = derive_shared_secret(root_ecc_key_id);
	if (device_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to derive shared secret! (%d)", status);
		return -EINVAL;
	}
#endif /* CONFIG_INFUSE_SECURE_STORAGE */

	/* Load root network key */
	network_root_key = network_key_load();
	if (network_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to load network root! (%d)", status);
		return -EINVAL;
	}
	return 0;
}

void infuse_security_cloud_public_key(uint8_t public_key[32])
{
	memcpy(public_key, infuse_cloud_public_key, sizeof(infuse_cloud_public_key));
}

void infuse_security_device_public_key(uint8_t public_key[32])
{
	psa_status_t status;
	size_t olen;

	status = psa_export_public_key(INFUSE_ROOT_ECC_KEY_ID, public_key, 32, &olen);
	if ((status != PSA_SUCCESS) || (olen != 32)) {
		LOG_ERR("Public key export failed (%d %d)", status, olen);
		memset(public_key, 0x00, 32);
	}
}

psa_key_id_t infuse_security_device_root_key(void)
{
	return device_root_key;
}

psa_key_id_t infuse_security_network_root_key(uint32_t *network_id)
{
	*network_id = cached_network_id;
	return network_root_key;
}
