/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/security.h>
#include <infuse/identifiers.h>
#include <infuse/crypto/hardware_unique_key.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/secure_storage.h>

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
static const uint8_t default_network_key[32] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
	0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

#ifdef CONFIG_TLS_CREDENTIALS
static uint8_t dtls_identity[8];
static uint8_t dtls_psk[32];
#endif /* CONFIG_TLS_CREDENTIALS */

static psa_key_id_t root_ecc_key_id, device_root_key, device_sign_key, network_root_key;
static uint32_t cached_network_id, cached_device_id;
static uint8_t device_public_key[32];

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
	psa_key_attributes_t key_attributes = hkdf_derive_attributes();
	uint8_t shared_secret[32];
	psa_status_t status;
	psa_key_id_t key_id;
	size_t olen;

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

		if (status != PSA_SUCCESS) {
			LOG_WRN("Failed to import %s root (%d)", "device", status);
			return PSA_KEY_ID_NULL;
		}
	}

	/* Calculate device key identifier (CRC32 over the two public keys) */
	cached_device_id = crc32_ieee(infuse_cloud_public_key, sizeof(infuse_cloud_public_key));
	cached_device_id =
		crc32_ieee_update(cached_device_id, device_public_key, sizeof(device_public_key));
	cached_device_id &= 0x00FFFFFF;

	return key_id;
}

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
	uint32_t salt = 0x1234;
	psa_status_t status;

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
	int rc;

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
		LOG_ERR("Failed to generate root key pair! (%d)", status);
		return -EINVAL;
	}
	/* Regenerate root shared secret */
	device_root_key = derive_shared_secret(root_ecc_key_id);
	if (device_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to derive shared secret! (%d)", status);
		return -EINVAL;
	}
	/* Derive */
	device_sign_key = infuse_security_derive_chacha_key(device_root_key, &salt, sizeof(salt),
							    "sign", 4, false);
	if (device_sign_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to derive signing key! (%d)", status);
		return -EINVAL;
	}

#ifdef CONFIG_TLS_CREDENTIALS
#ifdef CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS
	sys_put_le64(0xfffffffffffffffd, dtls_identity);

	{
		psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
		static const uint8_t shared_secret[32] = {
			0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

		psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
		psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
		psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
		psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
		psa_set_key_bits(&key_attributes, 256);

		status = psa_import_key(&key_attributes, shared_secret, sizeof(shared_secret),
					&device_root_key);
		if (status != PSA_SUCCESS) {
			LOG_ERR("Failed to import static shared secret (%d)", status);
			device_root_key = PSA_KEY_ID_NULL;
			return -EINVAL;
		}
	}
#else
	sys_put_le64(infuse_device_id(), dtls_identity);
#endif /* CONFIG_INFUSE_SECURITY_TEST_CREDENTIALS */
	psa_key_id_t dtls_coap_key;
	uint16_t dtls_coap_salt = 0x7856;
	size_t olen;

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

	rc = tls_credential_add(TLS_TAG_INFUSE_COAP, TLS_CREDENTIAL_PSK_ID, dtls_identity,
				sizeof(dtls_identity));
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

	/* Load root network key */
	network_root_key = network_key_load();
	if (network_root_key == PSA_KEY_ID_NULL) {
		LOG_ERR("Failed to load network root! (%d)", status);
		return -EINVAL;
	}
	return 0;
}

psa_key_id_t infuse_security_derive_chacha_key(psa_key_id_t base_key, const void *salt,
					       size_t salt_len, const void *info, size_t info_len,
					       bool force_export)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
	psa_key_id_t output_key = PSA_KEY_ID_NULL;
	psa_key_usage_t usage = PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT;

	if (IS_ENABLED(CONFIG_INFUSE_SECURITY_CHACHA_KEY_EXPORT) || force_export) {
		usage |= PSA_KEY_USAGE_EXPORT;
	}
	psa_set_key_usage_flags(&key_attributes, usage);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_CHACHA20_POLY1305);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&key_attributes, 256);

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

#ifdef CONFIG_TLS_CREDENTIALS
sec_tag_t infuse_security_coap_dtls_tag(void)
{
	return TLS_TAG_INFUSE_COAP;
}
#endif /* CONFIG_TLS_CREDENTIALS */
