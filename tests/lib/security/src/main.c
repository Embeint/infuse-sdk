/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <psa/internal_trusted_storage.h>

#include <infuse/security.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

static void default_init(void)
{
	infuse_security_init();

	/* Refresh network keys to default state */
	(void)infuse_security_network_key_write(0, NULL);
	(void)infuse_security_secondary_network_key_write(0, NULL);
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();
}

ZTEST(security, test_network_ids)
{
	default_init();

	/* Default network IDs */
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());
}

ZTEST(security, test_network_key_update)
{
	uint8_t key_material[32];
	uint32_t key_id_1 = 0xA5A5A5;
	uint32_t key_id_2 = 0x8B8B8B;
	uint32_t val = 0x12345678;
	int rc;

	default_init();

	sys_rand_get(key_material, sizeof(key_material));

	/* Write new network keys */
	rc = infuse_security_network_key_write(key_id_1, key_material);
	zassert_equal(0, rc);
	rc = infuse_security_secondary_network_key_write(key_id_2, key_material);
	zassert_equal(0, rc);

	/* By default nothing changes with the loaded values */
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Once the init function runs again, new keys are used */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();

	zassert_equal(key_id_1, infuse_security_network_key_identifier());
	zassert_equal(key_id_2, infuse_security_secondary_network_key_identifier());

	/* Writing NULL deletes the key information */
	rc = infuse_security_network_key_write(key_id_1, NULL);
	zassert_equal(0, rc);
	rc = infuse_security_secondary_network_key_write(key_id_2, NULL);
	zassert_equal(0, rc);
	rc = infuse_security_network_key_write(key_id_1, NULL);
	zassert_not_equal(0, rc);
	rc = infuse_security_secondary_network_key_write(key_id_2, NULL);
	zassert_not_equal(0, rc);

	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());

	/* Manually write bad data to the keys */
	rc = psa_its_set(KV_KEY_SECURE_STORAGE_RESERVED + 3, sizeof(val), &val,
			 PSA_STORAGE_FLAG_NONE);
	zassert_equal(0, rc);
	rc = psa_its_set(KV_KEY_SECURE_STORAGE_RESERVED + 4, sizeof(val), &val,
			 PSA_STORAGE_FLAG_NONE);
	zassert_equal(0, rc);

	/* Initialisation should fail and fallback to defaults */
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());
}

ZTEST(security, test_secondary_shared_secret)
{
#ifdef CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE
	struct kv_secondary_remote_public_key remote;
	psa_key_id_t primary_psa_id;
	psa_key_id_t secondary_psa_id;
	uint32_t primary_key_id;
	uint32_t secondary_key_id;

	default_init();

	primary_key_id = infuse_security_device_key_identifier();
	primary_psa_id = infuse_security_device_root_key();

	/* No secondary public key exists, values are NULL */
	secondary_key_id = infuse_security_secondary_device_key_identifier();
	secondary_psa_id = infuse_security_secondary_device_root_key();
	zassert_equal(0x00, secondary_key_id);
	zassert_equal(PSA_KEY_ID_NULL, secondary_psa_id);
	zassert_equal(-ENOENT, infuse_security_secondary_device_key_reset());

	/* Secondary public key written to KV store */
	sys_rand_get(remote.public_key, sizeof(remote.public_key));
	zassert_equal(sizeof(remote), KV_STORE_WRITE(KV_KEY_SECONDARY_REMOTE_PUBLIC_KEY, &remote));

	/* Shared secret not automatically generated */
	secondary_key_id = infuse_security_secondary_device_key_identifier();
	secondary_psa_id = infuse_security_secondary_device_root_key();
	zassert_equal(0x00, secondary_key_id);
	zassert_equal(PSA_KEY_ID_NULL, secondary_psa_id);

	/* Generated after init */
	infuse_security_init();

	secondary_key_id = infuse_security_secondary_device_key_identifier();
	secondary_psa_id = infuse_security_secondary_device_root_key();
	zassert_not_equal(0x00, secondary_key_id);
	zassert_not_equal(primary_key_id, secondary_key_id);
	zassert_not_equal(PSA_KEY_ID_NULL, secondary_psa_id);
	zassert_not_equal(primary_psa_id, secondary_psa_id);

	/* Should use cached value on next init */
	infuse_security_init();

	secondary_key_id = infuse_security_secondary_device_key_identifier();
	secondary_psa_id = infuse_security_secondary_device_root_key();
	zassert_not_equal(0x00, secondary_key_id);
	zassert_not_equal(PSA_KEY_ID_NULL, secondary_psa_id);

	/* Can delete cached device key */
	zassert_equal(0, infuse_security_secondary_device_key_reset());
	zassert_equal(-ENOENT, infuse_security_secondary_device_key_reset());
#else
	ztest_test_skip();
#endif /* CONFIG_INFUSE_SECURITY_SECONDARY_REMOTE_ENABLE */
}

ZTEST_SUITE(security, NULL, NULL, NULL, NULL, NULL);
