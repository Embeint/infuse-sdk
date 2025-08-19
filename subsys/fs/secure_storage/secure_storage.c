/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>

#if defined(CONFIG_KV_STORE_NVS)
#include <zephyr/fs/nvs.h>
#define ID_PRE 0
#define READ   nvs_read
#define WRITE  nvs_write
#define DELETE nvs_delete
#elif defined(CONFIG_KV_STORE_ZMS)
#include <zephyr/fs/zms.h>
#define ID_PRE (CONFIG_KV_STORE_ZMS_ID_PREFIX << 16)
#define READ   zms_read
#define WRITE  zms_write
#define DELETE zms_delete
#endif

#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/crypto/hardware_unique_key.h>

#include <psa/internal_trusted_storage.h>
#include <psa/error.h>

#define CHACHA_NONCE_SIZE 12
#define CHACHA_TAG_SIZE   16
#define MAX_PAYLOAD_SIZE  CONFIG_INFUSE_SECURE_STORAGE_MAX_SIZE
#define OVERHEAD          (sizeof(struct psa_storage_info_t) + CHACHA_NONCE_SIZE + CHACHA_TAG_SIZE)
#define DATA_SIZE         (MAX_PAYLOAD_SIZE + CHACHA_TAG_SIZE)

struct secure_storage_format {
	struct psa_storage_info_t info;
	uint8_t nonce[CHACHA_NONCE_SIZE];
	uint8_t data[DATA_SIZE];
};
static psa_key_id_t secure_storage_key_id;

LOG_MODULE_REGISTER(secure_storage, LOG_LEVEL_INF);

psa_status_t psa_its_set(psa_storage_uid_t uid, uint32_t data_length, const void *p_data,
			 psa_storage_create_flags_t create_flags)
{
	void *fs = kv_store_fs();
	struct secure_storage_format data;
	struct psa_storage_info_t info;
	psa_status_t status;
	size_t total_len = SIZE_MAX;
	size_t out_len;
	int rc;

	LOG_DBG("UID: %lld LEN: %d FLAGS: %08X", uid, data_length, create_flags);
	if ((uid < PSA_KEY_ID_INFUSE_MIN) || (uid > PSA_KEY_ID_INFUSE_MAX)) {
		return PSA_ERROR_INVALID_HANDLE;
	}

	if (data_length > MAX_PAYLOAD_SIZE) {
		return PSA_ERROR_INSUFFICIENT_STORAGE;
	}

	/* Check WRITE_ONCE flag */
	rc = READ(fs, ID_PRE | uid, &info, sizeof(info));
	if ((rc > 0) && (info.flags & PSA_STORAGE_FLAG_WRITE_ONCE)) {
		LOG_ERR("Writing to WRITE_ONCE ID");
		return PSA_ERROR_NOT_PERMITTED;
	}

	/* Populate header and nonce */
	data.info.flags = create_flags;
	data.info.size = data_length;
	sys_csrand_get(data.nonce, sizeof(data.nonce));

	/* Encrypt data */
	status = psa_aead_encrypt(secure_storage_key_id, PSA_ALG_CHACHA20_POLY1305, data.nonce,
				  sizeof(data.nonce), (uint8_t *)&data.info, sizeof(data.info),
				  p_data, data_length, data.data, sizeof(data.data), &out_len);
	if (status != PSA_SUCCESS) {
		return PSA_ERROR_STORAGE_FAILURE;
	}

	/* Write data to NVS */
	total_len = sizeof(data.info) + sizeof(data.nonce) + out_len;
	rc = WRITE(fs, ID_PRE | uid, &data, total_len);

	/* Cleanup stack variables */
	mbedtls_platform_zeroize(&data, sizeof(data));

	/* Return result */
	return rc == total_len ? PSA_SUCCESS : PSA_ERROR_HARDWARE_FAILURE;
}

psa_status_t psa_its_get(psa_storage_uid_t uid, uint32_t data_offset, uint32_t data_length,
			 void *p_data, size_t *p_data_length)
{
	uint8_t decrypt_buf[CONFIG_INFUSE_SECURE_STORAGE_MAX_SIZE];
	void *fs = kv_store_fs();
	struct secure_storage_format data;
	psa_status_t status = PSA_SUCCESS;
	size_t data_len, out_len;
	ssize_t rc;

	*p_data_length = 0;

	LOG_DBG("UID: %lld OFF: %d LEN: %d", uid, data_offset, data_length);
	if ((uid < PSA_KEY_ID_INFUSE_MIN) || (uid > PSA_KEY_ID_INFUSE_MAX)) {
		return PSA_ERROR_INVALID_HANDLE;
	}

	if (data_length == 0) {
		return PSA_SUCCESS;
	}

	/* Read data from NVS */
	rc = READ(fs, ID_PRE | uid, &data, sizeof(data));
	if (rc == -ENOENT) {
		status = PSA_ERROR_DOES_NOT_EXIST;
		goto cleanup;
	}
	if (rc < 0) {
		status = PSA_ERROR_HARDWARE_FAILURE;
		goto cleanup;
	}
	/* Ensure enough data */
	if (rc <= OVERHEAD) {
		status = PSA_ERROR_DATA_CORRUPT;
		goto cleanup;
	}
	data_len = rc - OVERHEAD;
	/* Sizes should match */
	if (data.info.size != data_len) {
		status = PSA_ERROR_DATA_CORRUPT;
		goto cleanup;
	}
	/* All data lies outside valid memory */
	if (data_offset >= data_len) {
		status = PSA_ERROR_INSUFFICIENT_DATA;
		goto cleanup;
	}
	/* Decrypt the content */
	status = psa_aead_decrypt(secure_storage_key_id, PSA_ALG_CHACHA20_POLY1305, data.nonce,
				  sizeof(data.nonce), (uint8_t *)&data.info, sizeof(data.info),
				  data.data, data_len + CHACHA_TAG_SIZE, decrypt_buf,
				  sizeof(decrypt_buf), &out_len);
	if (status != PSA_SUCCESS) {
		status = PSA_ERROR_DATA_CORRUPT;
		goto cleanup;
	}

	/* Limit requested data to valid memory */
	if ((data_offset + data_length) > out_len) {
		data_length = out_len - data_offset;
	}

	memcpy(p_data, decrypt_buf + data_offset, data_length);
	*p_data_length = data_length;

cleanup:
	/* Cleanup stack variables */
	mbedtls_platform_zeroize(&decrypt_buf, sizeof(decrypt_buf));

	/* Return result */
	return status;
}

psa_status_t psa_its_get_info(psa_storage_uid_t uid, struct psa_storage_info_t *p_info)
{
	void *fs = kv_store_fs();
	ssize_t expected, rc;

	LOG_DBG("UID: %lld", uid);
	if ((uid < PSA_KEY_ID_INFUSE_MIN) || (uid > PSA_KEY_ID_INFUSE_MAX)) {
		return PSA_ERROR_INVALID_HANDLE;
	}

	/* Read off header info */
	rc = READ(fs, ID_PRE | uid, p_info, sizeof(*p_info));
	if (rc == -ENOENT) {
		return PSA_ERROR_DOES_NOT_EXIST;
	} else if (rc < 0) {
		return PSA_ERROR_HARDWARE_FAILURE;
	}
	expected = (sizeof(*p_info) + CHACHA_NONCE_SIZE + p_info->size + CHACHA_TAG_SIZE);
#ifdef CONFIG_KV_STORE_ZMS
	/* ZMS has different return value semantics for reading less than the complete value */
	if ((rc != sizeof(*p_info)) || (expected != zms_get_data_length(fs, ID_PRE | uid))) {
		return PSA_ERROR_DATA_CORRUPT;
	}
#else
	if (rc != expected) {
		return PSA_ERROR_DATA_CORRUPT;
	}
#endif /* CONFIG_KV_STORE_ZMS */
	return PSA_SUCCESS;
}

psa_status_t psa_its_remove(psa_storage_uid_t uid)
{
	struct psa_storage_info_t info;
	void *fs = kv_store_fs();
	ssize_t rc;

	LOG_DBG("UID: %lld", uid);
	if ((uid < PSA_KEY_ID_INFUSE_MIN) || (uid > PSA_KEY_ID_INFUSE_MAX)) {
		return PSA_ERROR_INVALID_HANDLE;
	}

	/* Read off header info */
	rc = READ(fs, ID_PRE | uid, &info, sizeof(info));
	if (rc == -ENOENT) {
		return PSA_ERROR_DOES_NOT_EXIST;
	} else if (rc < 0) {
		return PSA_ERROR_HARDWARE_FAILURE;
	}

	/* Check WRITE_ONCE flag */
	if (info.flags & PSA_STORAGE_FLAG_WRITE_ONCE) {
		LOG_ERR("Erasing WRITE_ONCE ID");
		return PSA_ERROR_NOT_PERMITTED;
	}

	/* Erase value */
	rc = DELETE(fs, ID_PRE | uid);
	return rc == 0 ? PSA_SUCCESS : PSA_ERROR_HARDWARE_FAILURE;
}

int secure_storage_init(void)
{
	psa_key_id_t huk_id = hardware_unique_key_id();
	const char *const info = "SECURE_STORAGE";
	const char *const salt = "SS_SALT";

	/* Derive secure storage key from HUK */
	secure_storage_key_id = infuse_security_derive_chacha_key(huk_id, salt, strlen(salt), info,
								  strlen(info), false);
	return secure_storage_key_id == PSA_KEY_ID_NULL ? -EINVAL : 0;
}
