/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/security.h>
#include <infuse/identifiers.h>
#include <infuse/crypto/hardware_unique_key.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/secure_storage.h>

#include <infuse/network_key.h>

#ifdef CONFIG_MODEM_KEY_MGMT
#include "modem/modem_key_mgmt.h"
#endif

#include <psa/crypto.h>
#if defined(CONFIG_INFUSE_SECURE_STORAGE) || defined(CONFIG_BUILD_WITH_TFM)
#include <psa/internal_trusted_storage.h>
#define ITS_AVAILABLE 1
#endif
#include <mbedtls/platform_util.h>

enum {
	INFUSE_ROOT_ECC_KEY_ID = KV_KEY_SECURE_STORAGE_RESERVED,
	INFUSE_ROOT_ECC_PUBLIC_KEY_ID,
	INFUSE_ROOT_ECC_SHARED_SECRET_KEY_ID,
	INFUSE_ROOT_NETWORK_KEY_ID,
};

#define TLS_TAG_INFUSE_COAP 12

static const uint8_t infuse_cloud_public_key[32] = {
	0xca, 0x66, 0x32, 0xab, 0x03, 0x81, 0x72, 0xb6, 0xef, 0x6a, 0x05,
	0x40, 0xd0, 0x8b, 0xc7, 0x2e, 0x9c, 0xce, 0x29, 0x36, 0x68, 0xdf,
	0xa8, 0x7c, 0xd5, 0x1d, 0x64, 0x74, 0x1c, 0x53, 0xe0, 0x0a,
};

static psa_key_id_t root_ecc_key_id, device_root_key, device_sign_key, network_root_key;
static uint32_t cached_network_id, cached_device_id;
static uint8_t device_public_key[32];

#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE

#include <infuse/network_key_secondary.h>

static psa_key_id_t secondary_network_root_key;
static uint32_t secondary_cached_network_id;

#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */

LOG_MODULE_REGISTER(security, LOG_LEVEL_INF);

psa_key_attributes_t infuse_security_hkdf_attributes(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 256);

	return key_attributes;
}

static void device_public_key_export(void)
{
	psa_status_t status;
	size_t olen;

#ifdef ITS_AVAILABLE
	/* Try to load cached public key */
	status = psa_its_get(INFUSE_ROOT_ECC_PUBLIC_KEY_ID, 0, 32, device_public_key, &olen);
	if ((status == PSA_SUCCESS) && (olen == 32)) {
		return;
	}
#endif /* ITS_AVAILABLE */

	/* Export public key and save */
	status = psa_export_public_key(INFUSE_ROOT_ECC_KEY_ID, device_public_key, 32, &olen);
	if ((status != PSA_SUCCESS) || (olen != 32)) {
		LOG_ERR("Public key export failed (%d %d)", status, olen);
		memset(device_public_key, 0x00, 32);
	}
#ifdef ITS_AVAILABLE
	else {
		status = psa_its_set(INFUSE_ROOT_ECC_PUBLIC_KEY_ID, 32, device_public_key,
				     PSA_STORAGE_FLAG_NONE);
		if (status != PSA_SUCCESS) {
			LOG_ERR("Failed to save public key (%d)", status);
		}
	}
#endif /* ITS_AVAILABLE */
}

static psa_key_id_t generate_root_ecc_key_pair(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;
	psa_key_id_t key_id;

	/* Attempt to open the key before spending time generating it */
	status = psa_open_key(INFUSE_ROOT_ECC_KEY_ID, &key_id);
	if (status != PSA_SUCCESS) {
#ifdef ITS_AVAILABLE
		/* Remove any existing derived keys */
		(void)psa_its_remove(INFUSE_ROOT_ECC_PUBLIC_KEY_ID);
		(void)psa_its_remove(INFUSE_ROOT_ECC_SHARED_SECRET_KEY_ID);
#endif /* ITS_AVAILABLE */
#ifdef CONFIG_MODEM_KEY_MGMT
		/* COAP PSK is also a derived key */
		(void)modem_key_mgmt_delete(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY);
		(void)modem_key_mgmt_delete(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_PSK);
#endif /* CONFIG_MODEM_KEY_MGMT */

		/* ECDH, Curve25519 */
		psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
		psa_set_key_type(&key_attributes,
				 PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
		psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDH);
		psa_set_key_bits(&key_attributes, 255);

		psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
		psa_set_key_id(&key_attributes, INFUSE_ROOT_ECC_KEY_ID);

		status = psa_generate_key(&key_attributes, &key_id);
		if (status != PSA_SUCCESS) {
			LOG_ERR("Failed to generate root ECDH key (%d)", status);
			key_id = PSA_KEY_ID_NULL;
		}
	}

	/* Export public key once */
	if (key_id == INFUSE_ROOT_ECC_KEY_ID) {
		device_public_key_export();
	}

	return key_id;
}

static psa_key_id_t derive_shared_secret(psa_key_id_t root_key_id)
{
	psa_key_attributes_t key_attributes = infuse_security_hkdf_attributes();
	uint8_t __maybe_unused shared_secret[32];
	size_t __maybe_unused olen;
	psa_status_t status;
	psa_key_id_t key_id;

#ifdef CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS
	static const uint8_t test_shared_secret[32] = {
		0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};

	status = psa_import_key(&key_attributes, test_shared_secret, sizeof(test_shared_secret),
				&key_id);
#else
	/* Attempt to open the key before spending time generating it */
	status = psa_open_key(INFUSE_ROOT_ECC_SHARED_SECRET_KEY_ID, &key_id);
	if (status != PSA_SUCCESS) {
		/* Calculate shared secret */
		status = psa_raw_key_agreement(PSA_ALG_ECDH, root_key_id, infuse_cloud_public_key,
					       sizeof(infuse_cloud_public_key), shared_secret,
					       sizeof(shared_secret), &olen);
		if (status != PSA_SUCCESS) {
			return PSA_KEY_ID_NULL;
		}
		/* Override lifetime to persistent */
		psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
		/* Set the persistent key ID */
		psa_set_key_id(&key_attributes, INFUSE_ROOT_ECC_SHARED_SECRET_KEY_ID);
		/* Import device shared key into PSA */
		status = psa_import_key(&key_attributes, shared_secret, sizeof(shared_secret),
					&key_id);
		/* Clear sensitive stack content */
		mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));
	}
#endif /* CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS */
	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "device", status);
		return PSA_KEY_ID_NULL;
	}

	/* Calculate device key identifier (CRC32 over the two public keys) */
	cached_device_id = crc32_ieee(infuse_cloud_public_key, sizeof(infuse_cloud_public_key));
	cached_device_id =
		crc32_ieee_update(cached_device_id, device_public_key, sizeof(device_public_key));
	cached_device_id &= 0x00FFFFFF;

#ifdef CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS
	/* This is the device ID the cloud server expects for test_shared_secret */
	cached_device_id = 0x2F33D3;
#endif
	return key_id;
}

static int coap_dtls_load(void)
{
#if defined(CONFIG_TLS_CREDENTIALS) || defined(CONFIG_MODEM_KEY_MGMT)
#ifdef CONFIG_TLS_CREDENTIALS
	/* TLS credential library needs values to persist */
	static char dtls_identity_str[16 + 1];
	static uint8_t dtls_psk[32];
#else
	char dtls_identity_str[16 + 1];
	char dtls_psk_str[64 + 1];
	uint8_t dtls_psk[32];
#endif /* CONFIG_TLS_CREDENTIALS */
	uint16_t dtls_coap_salt = 0x7856;
	psa_key_id_t dtls_coap_key;
	psa_status_t status;
	size_t olen;
	int rc;

#ifdef CONFIG_MODEM_KEY_MGMT
	olen = sizeof(dtls_identity_str);

	rc = modem_key_mgmt_read(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY,
				 dtls_identity_str, &olen);
	if (rc == 0 && (olen == 16)) {
		char expected[16 + 1];

		/* Compare identity against what we would expect based on device ID */
		snprintf(expected, sizeof(expected), "%016" PRIx64, infuse_device_id());
		if (memcmp(dtls_identity_str, expected, 16) == 0) {
			/* Key exists & identity matches */
			return 0;
		}

		/* Reset the credentials */
		(void)modem_key_mgmt_delete(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY);
		(void)modem_key_mgmt_delete(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_PSK);
	}
#endif /* CONFIG_MODEM_KEY_MGMT */

	/* Initialise identity from Infuse ID */
	snprintf(dtls_identity_str, sizeof(dtls_identity_str), "%016" PRIx64, infuse_device_id());

	/* Derive Infuse-IoT COAP key */
	dtls_coap_key = infuse_security_derive_chacha_key(device_root_key, &dtls_coap_salt,
							  sizeof(dtls_coap_salt), "coap", 4, true);
	if (dtls_coap_key == PSA_KEY_ID_NULL) {
		LOG_ERR("COAP key derivation failed");
		return -EINVAL;
	}

	/* Export key back into buffer for TLS credentials */
	status = psa_export_key(dtls_coap_key, dtls_psk, sizeof(dtls_psk), &olen);
	if ((status != PSA_SUCCESS) || (olen != 32)) {
		LOG_ERR("COAP key export failed (%d %d)", status, olen);
		memset(dtls_psk, 0x00, 32);
		return -EINVAL;
	}

	/* No need to hold onto the derived key after export */
	(void)psa_destroy_key(dtls_coap_key);

#ifdef CONFIG_TLS_CREDENTIALS
	rc = tls_credential_add(TLS_TAG_INFUSE_COAP, TLS_CREDENTIAL_PSK_ID, dtls_identity_str,
				strlen(dtls_identity_str));
	if (rc < 0) {
		LOG_ERR("Failed to add DTLS identity (%d)", rc);
		return -EINVAL;
	}

	rc = tls_credential_add(TLS_TAG_INFUSE_COAP, TLS_CREDENTIAL_PSK, dtls_psk,
				sizeof(dtls_psk));
	if (rc < 0) {
		LOG_ERR("Failed to add DTLS PSK (%d)", rc);
		return -EINVAL;
	}
#endif /* CONFIG_TLS_CREDENTIALS */
#ifdef CONFIG_MODEM_KEY_MGMT

	/* Format 128 bit key as a hex string */
	for (int i = 0; i < 32; i++) {
		sprintf(dtls_psk_str + (2 * i), "%02x", dtls_psk[i]);
	}

	/* Write key to modem */
	rc = modem_key_mgmt_write(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_IDENTITY,
				  dtls_identity_str, strlen(dtls_identity_str));
	if (rc < 0) {
		LOG_ERR("Failed to add DTLS identity (%d)", rc);
		return -EINVAL;
	}
	rc = modem_key_mgmt_write(TLS_TAG_INFUSE_COAP, MODEM_KEY_MGMT_CRED_TYPE_PSK, dtls_psk_str,
				  strlen(dtls_psk_str));
	if (rc < 0) {
		LOG_ERR("Failed to add DTLS PSK (%d)", rc);
		return -EINVAL;
	}

#endif /* CONFIG_MODEM_KEY_MGMT */
#endif /* defined(CONFIG_TLS_CREDENTIALS) || defined(CONFIG_MODEM_KEY_MGMT) */
	return 0;
}

static psa_key_id_t explicit_key_load(const uint8_t *key, uint8_t key_len)
{
	psa_key_attributes_t key_attributes = infuse_security_hkdf_attributes();
	psa_status_t status;
	psa_key_id_t key_id;

	status = psa_import_key(&key_attributes, key, key_len, &key_id);
	if (status != PSA_SUCCESS) {
		LOG_WRN("Failed to import %s root (%d)", "root", status);
		return PSA_KEY_ID_NULL;
	}

	return key_id;
}

int infuse_security_init(void)
{
	uint32_t salt = 0x1234;
	psa_status_t status;
	int rc;

	if (IS_ENABLED(CONFIG_INFUSE_SECURITY_SKIP_INIT)) {
		return 0;
	}

	/* Initialise crypto system */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_ERR("PSA init failed! (%d)", status);
		return -EINVAL;
	}

	/* Initialise hardware unique key */
	if (hardware_unique_key_init() < 0) {
		return -EINVAL;
	}

#ifdef CONFIG_INFUSE_SECURE_STORAGE
	/* Initialise secure storage  */
	rc = secure_storage_init();
	if (rc < 0) {
		LOG_ERR("Failed to init secure storage! (%d)", rc);
		return -EINVAL;
	}
#endif /* CONFIG_INFUSE_SECURE_STORAGE */

	/* Create/import device root ECC key pair */
	root_ecc_key_id = generate_root_ecc_key_pair();
	if (root_ecc_key_id == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to generate root key pair!");
		return -EINVAL;
	}
	/* Regenerate root shared secret */
	device_root_key = derive_shared_secret(root_ecc_key_id);
	if (device_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to derive shared secret!");
		return -EINVAL;
	}
	/* Derive signing key */
	device_sign_key = infuse_security_derive_chacha_key(device_root_key, &salt, sizeof(salt),
							    "sign", 4, false);
	if (device_sign_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to derive signing key!");
		return -EINVAL;
	}

	/* Load COAP key */
	rc = coap_dtls_load();
	if (rc < 0) {
		return rc;
	}

	/* Load root network key */
	cached_network_id = INFUSE_NETWORK_KEY_ID;
	network_root_key = explicit_key_load(infuse_network_key, sizeof(infuse_network_key));
	if (network_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to load network root!");
		return -EINVAL;
	}

#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE
	/* Load secondary network key */
	secondary_cached_network_id = SECONDARY_NETWORK_KEY_ID;
	secondary_network_root_key =
		explicit_key_load(secondary_network_key, sizeof(secondary_network_key));
	if (secondary_network_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to load secondary network root!");
		return -EINVAL;
	}
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */

	return 0;
}

psa_key_id_t infuse_security_derive_key(psa_key_id_t base_key, psa_algorithm_t algorithm,
					psa_key_type_t key_type, size_t key_bits,
					psa_key_usage_t key_usage, const void *salt,
					size_t salt_len, const void *info, size_t info_len,
					bool force_export)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
	psa_key_id_t output_key = PSA_KEY_ID_NULL;

	if (IS_ENABLED(CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT) || force_export) {
		key_usage |= PSA_KEY_USAGE_EXPORT;
	}
	psa_set_key_usage_flags(&key_attributes, key_usage);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, algorithm);
	psa_set_key_type(&key_attributes, key_type);
	psa_set_key_bits(&key_attributes, key_bits);

	if (psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256)) ||
	    psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, salt,
					   salt_len) ||
	    psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO, info,
					   info_len) ||
	    psa_key_derivation_input_key(&operation, PSA_KEY_DERIVATION_INPUT_SECRET, base_key) ||
	    psa_key_derivation_output_key(&key_attributes, &operation, &output_key)) {
		output_key = PSA_KEY_ID_NULL;
	}
	psa_key_derivation_abort(&operation);
	return output_key;
}

psa_key_id_t infuse_security_derive_chacha_key(psa_key_id_t base_key, const void *salt,
					       size_t salt_len, const void *info, size_t info_len,
					       bool force_export)
{
	return infuse_security_derive_key(base_key, PSA_ALG_CHACHA20_POLY1305,
					  PSA_KEY_TYPE_CHACHA20, 256,
					  PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, salt,
					  salt_len, info, info_len, force_export);
}

void infuse_security_cloud_public_key(uint8_t public_key[32])
{
	memcpy(public_key, infuse_cloud_public_key, sizeof(infuse_cloud_public_key));
}

void infuse_security_device_public_key(uint8_t public_key[32])
{
	memcpy(public_key, device_public_key, sizeof(device_public_key));
}

psa_key_id_t infuse_security_device_root_key(void)
{
	return device_root_key;
}

psa_key_id_t infuse_security_device_sign_key(void)
{
	return device_sign_key;
}

psa_key_id_t infuse_security_network_root_key(void)
{
	return network_root_key;
}

uint32_t infuse_security_device_key_identifier(void)
{
	return cached_device_id;
}

uint32_t infuse_security_network_key_identifier(void)
{
	return cached_network_id;
}

#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE

psa_key_id_t infuse_security_secondary_network_root_key(void)
{
	return secondary_network_root_key;
}

uint32_t infuse_security_secondary_network_key_identifier(void)
{
	return secondary_cached_network_id;
}

#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE */

#if defined(CONFIG_TLS_CREDENTIALS) || defined(CONFIG_NRF_MODEM_LIB)
sec_tag_t infuse_security_coap_dtls_tag(void)
{
	return TLS_TAG_INFUSE_COAP;
}
#endif /* defined(CONFIG_TLS_CREDENTIALS) || defined(CONFIG_NRF_MODEM_LIB) */
