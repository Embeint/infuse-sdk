/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

#if defined(CONFIG_KV_STORE_NVS)
#define ID_PRE 0
#include <zephyr/fs/nvs.h>
#define WRITE nvs_write
#define READ  nvs_read
#elif defined(CONFIG_KV_STORE_ZMS)
#define ID_PRE (CONFIG_KV_STORE_ZMS_ID_PREFIX << 16)
#include <zephyr/fs/zms.h>
#define WRITE zms_write
#define READ  zms_read
#else
#error Unknown KV store backend
#endif

#include <infuse/fs/secure_storage.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/crypto/hardware_unique_key.h>

#include <psa/crypto.h>
#include <psa/internal_trusted_storage.h>

int kv_store_init(void);

static const uint8_t ecdh_public_key[] = {
	0xc2, 0xfc, 0x16, 0x76, 0xa5, 0xda, 0x15, 0x38, 0x8e, 0x64, 0x26,
	0x99, 0x83, 0xbf, 0xa6, 0x28, 0xfd, 0x9b, 0xfa, 0x94, 0xca, 0x51,
	0x58, 0x78, 0xec, 0x8f, 0xdb, 0xdb, 0x94, 0xb6, 0x3b, 0x44,
};

ZTEST(secure_storage_psa, test_invalid_ids)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;
	psa_key_id_t key_id;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDH);
	psa_set_key_bits(&key_attributes, 255);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);

	/* Creating keys outside the reserved range should fail */
	psa_set_key_id(&key_attributes, KV_KEY_SECURE_STORAGE_RESERVED - 1);
	status = psa_generate_key(&key_attributes, &key_id);
	zassert_not_equal(PSA_SUCCESS, status, "Generated key with invalid ID");

	psa_set_key_id(&key_attributes, KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1);
	status = psa_generate_key(&key_attributes, &key_id);
	zassert_not_equal(PSA_SUCCESS, status, "Generated key with invalid ID");

	/* Destroying keys outside the reserved range should fail */
	status = psa_destroy_key(KV_KEY_SECURE_STORAGE_RESERVED - 1);
	zassert_not_equal(PSA_SUCCESS, status, "Deleted key with invalid ID");

	status = psa_destroy_key(KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1);
	zassert_not_equal(PSA_SUCCESS, status, "Deleted key with invalid ID");
}

ZTEST(secure_storage_psa, test_ecdh_persistent_key)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t valid_key_id = KV_KEY_SECURE_STORAGE_RESERVED;
	psa_status_t status;
	psa_key_id_t key_id;
	uint8_t secret[32];
	size_t olen;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDH);
	psa_set_key_bits(&key_attributes, 255);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
	psa_set_key_id(&key_attributes, valid_key_id);

	/* Creating key should work */
	status = psa_generate_key(&key_attributes, &key_id);
	zassert_equal(PSA_SUCCESS, status, "Failed to generate persistent ECDH key");
	zassert_equal(valid_key_id, key_id);

	/* Use key for some operation */
	status = psa_raw_key_agreement(PSA_ALG_ECDH, valid_key_id, ecdh_public_key,
				       sizeof(ecdh_public_key), secret, sizeof(secret), &olen);
	zassert_equal(PSA_SUCCESS, status, "Failed to use persistent ECDH key");

	/* Creating key again should fail */
	status = psa_generate_key(&key_attributes, &key_id);
	zassert_equal(PSA_ERROR_ALREADY_EXISTS, status, "Failed to generate persistent ECDH key");

	/* Deleting key should work */
	status = psa_destroy_key(valid_key_id);
	zassert_equal(PSA_SUCCESS, status, "Failed to destroy persistent ECDH key");
}

static void *kv_setup(void)
{
	kv_store_init();
	psa_crypto_init();
	hardware_unique_key_init();
	secure_storage_init();
	return NULL;
}

static void kv_before(void *fixture)
{
	kv_store_reset();
}

/* Usage through the PSA APIs */
ZTEST_SUITE(secure_storage_psa, NULL, kv_setup, kv_before, NULL, NULL);

ZTEST(secure_storage_its, test_invalid_ids)
{
	size_t dlen;

	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_set(KV_KEY_SECURE_STORAGE_RESERVED - 1, 0, NULL, 0));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_set(KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1, 0, NULL, 0));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_get(KV_KEY_SECURE_STORAGE_RESERVED - 1, 0, 0, NULL, &dlen));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_get(KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1, 0, 0, NULL, &dlen));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_get_info(KV_KEY_SECURE_STORAGE_RESERVED - 1, NULL));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_get_info(KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1, NULL));
	zassert_equal(PSA_ERROR_INVALID_HANDLE, psa_its_remove(KV_KEY_SECURE_STORAGE_RESERVED - 1));
	zassert_equal(PSA_ERROR_INVALID_HANDLE,
		      psa_its_remove(KV_KEY_SECURE_STORAGE_RESERVED_MAX + 1));
}

ZTEST(secure_storage_its, test_ops_no_data)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	struct psa_storage_info_t info;
	uint8_t data[16];
	size_t dlen;

	zassert_equal(PSA_ERROR_DOES_NOT_EXIST, psa_its_get_info(key, &info));
	zassert_equal(PSA_ERROR_DOES_NOT_EXIST, psa_its_get(key, 0, sizeof(data), data, &dlen));
	zassert_equal(0, dlen);
	zassert_equal(PSA_ERROR_DOES_NOT_EXIST, psa_its_remove(key));
}

ZTEST(secure_storage_its, test_read)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	struct psa_storage_info_t info;
	uint8_t data_in[16];
	uint8_t data_out[32];
	size_t dlen;

	for (int i = 0; i < sizeof(data_in); i++) {
		data_in[i] = i;
	}

	/* Write data */
	zassert_equal(PSA_SUCCESS, psa_its_set(key, sizeof(data_in), data_in, 0x00));

	/* Validate info */
	zassert_equal(PSA_SUCCESS, psa_its_get_info(key, &info));
	zassert_equal(0x00, info.flags);
	zassert_equal(sizeof(data_in), info.size);

	/* Read all data */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 0, sizeof(data_in), data_out, &dlen));
	zassert_equal(sizeof(data_in), dlen);
	zassert_mem_equal(data_in, data_out, dlen);

	/* Read no data */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 0, 0, data_out, &dlen));
	zassert_equal(0, dlen);

	/* Request more than written */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 0, sizeof(data_out), data_out, &dlen));
	zassert_equal(sizeof(data_in), dlen);
	zassert_mem_equal(data_in, data_out, dlen);

	/* Request with offset */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 8, sizeof(data_in) - 8, data_out, &dlen));
	zassert_equal(sizeof(data_in) - 8, dlen);
	zassert_mem_equal(data_in + 8, data_out, dlen);

	/* Request with offset that runs over */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 8, sizeof(data_in), data_out, &dlen));
	zassert_equal(sizeof(data_in) - 8, dlen);
	zassert_mem_equal(data_in + 8, data_out, dlen);

	/* Request after all valid data */
	zassert_equal(PSA_ERROR_INSUFFICIENT_DATA,
		      psa_its_get(key, 16, sizeof(data_in), data_out, &dlen));
	zassert_equal(0, dlen);
}

ZTEST(secure_storage_its, test_write_too_much)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	uint8_t data_in[CONFIG_INFUSE_SECURE_STORAGE_MAX_SIZE + 1] = {0};

	zassert_equal(PSA_ERROR_INSUFFICIENT_STORAGE,
		      psa_its_set(key, sizeof(data_in), data_in, 0x00));
}

ZTEST(secure_storage_its, test_write_once)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	struct psa_storage_info_t info;
	uint8_t data_in[16];
	uint8_t data_out[32];
	size_t dlen;

	for (int i = 0; i < sizeof(data_in); i++) {
		data_in[i] = i;
	}

	/* Write data */
	zassert_equal(PSA_SUCCESS,
		      psa_its_set(key, sizeof(data_in), data_in, PSA_STORAGE_FLAG_WRITE_ONCE));
	/* Try to write again */
	zassert_equal(PSA_ERROR_NOT_PERMITTED,
		      psa_its_set(key, sizeof(data_in), data_in, PSA_STORAGE_FLAG_WRITE_ONCE));
	/* Try to delete */
	zassert_equal(PSA_ERROR_NOT_PERMITTED, psa_its_remove(key));

	/* Validate info */
	zassert_equal(PSA_SUCCESS, psa_its_get_info(key, &info));
	zassert_equal(PSA_STORAGE_FLAG_WRITE_ONCE, info.flags);
	zassert_equal(sizeof(data_in), info.size);

	/* Read the data back */
	zassert_equal(PSA_SUCCESS, psa_its_get(key, 0, sizeof(data_in), data_out, &dlen));
	zassert_equal(sizeof(data_in), dlen);
	zassert_mem_equal(data_in, data_out, dlen);
}

ZTEST(secure_storage_its, test_corrupt_length)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	void *fs = kv_store_fs();
	struct corrupted_length {
		struct psa_storage_info_t info;
		uint8_t nonce[12];
		uint8_t data[8];
		uint8_t tag[16];
	} corrupt;
	struct psa_storage_info_t info;
	size_t dlen;

	corrupt.info.flags = 0;
	corrupt.info.size = 4;

	/* Write data with invalid size information */
	zassert_equal(sizeof(corrupt), WRITE(fs, ID_PRE | key, &corrupt, sizeof(corrupt)));

	/* Get functions can detect */
	zassert_equal(PSA_ERROR_DATA_CORRUPT, psa_its_get_info(key, &info));
	zassert_equal(PSA_ERROR_DATA_CORRUPT,
		      psa_its_get(key, 0, sizeof(corrupt), &corrupt, &dlen));

	/* Write data with invalid length */
	zassert_equal(13, WRITE(fs, ID_PRE | key, &corrupt, 13));

	/* Get functions can detect */
	zassert_equal(PSA_ERROR_DATA_CORRUPT, psa_its_get_info(key, &info));
	zassert_equal(PSA_ERROR_DATA_CORRUPT,
		      psa_its_get(key, 0, sizeof(corrupt), &corrupt, &dlen));
}

ZTEST(secure_storage_its, test_malicious_corruption)
{
	psa_storage_uid_t key = KV_KEY_SECURE_STORAGE_RESERVED;
	void *fs = kv_store_fs();
	uint8_t data_in[16];
	uint8_t data_out[64];
	size_t dlen;
	ssize_t rc;

	for (int i = 0; i < sizeof(data_in); i++) {
		data_in[i] = i;
	}

	/* Write initial data */
	zassert_equal(PSA_SUCCESS, psa_its_set(key, sizeof(data_in), data_in, 0x00));

	/* Read raw data out of filesystem */
	rc = READ(fs, ID_PRE | key, data_out, sizeof(data_out));
	/* Corrupt a byte */
	data_out[8] += 1;
	/* Write corrupted data back to filesystem */
	zassert_equal(rc, WRITE(fs, ID_PRE | key, data_out, rc));

	/* Data corruption should be detected */
	zassert_equal(PSA_ERROR_DATA_CORRUPT,
		      psa_its_get(key, 0, sizeof(data_out), &data_out, &dlen));
}

/* Direct calls to ITS API */
ZTEST_SUITE(secure_storage_its, NULL, kv_setup, kv_before, NULL, NULL);
