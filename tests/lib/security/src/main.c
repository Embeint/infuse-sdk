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

ZTEST(security_network_keys, test_network_ids)
{
	/* Default network IDs */
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());
}

ZTEST(security_network_keys, test_key_update)
{
	uint8_t key_material[32];
	uint32_t key_id_1 = 0xA5A5A5;
	uint32_t key_id_2 = 0x8B8B8B;
	uint32_t val = 0x12345678;
	int rc;

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

static bool security_init(const void *global_state)
{
	infuse_security_init();
	return true;
}

static void test_before(void *fixture)
{
	/* Refresh network keys to default state */
	(void)infuse_security_network_key_write(0, NULL);
	(void)infuse_security_secondary_network_key_write(0, NULL);
	infuse_security_network_keys_unload();
	infuse_security_network_keys_load();
}

ZTEST_SUITE(security_network_keys, security_init, NULL, test_before, NULL, NULL);
