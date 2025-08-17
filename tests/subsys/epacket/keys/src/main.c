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

#include <infuse/security.h>
#include <infuse/epacket/keys.h>

#define KEY_SIZE 32

/* How many bits differ between two arrays */
static uint32_t bit_difference(const void *a, const void *b, size_t size)
{
	const uint8_t *a1 = a;
	const uint8_t *b1 = b;
	uint32_t difference = 0;

	for (int i = 0; i < size; i++) {
		difference += POPCOUNT(a1[i] ^ b1[i]);
	}
	return difference;
}

/* For 256-bit keys the average binary difference should be 128 bits.
 * For validation purposes accept anything in the range 96 to 160.
 */
static bool keys_different(uint8_t *a, uint8_t *b)
{
	uint32_t difference = bit_difference(a, b, KEY_SIZE);

	return (difference >= 96) && (difference <= 160);
}

ZTEST(epacket_keys, test_bit_difference)
{
	uint32_t a = 0, b = UINT32_MAX;

	zassert_equal(0, bit_difference(&a, &a, sizeof(a)), "");
	zassert_equal(0, bit_difference(&b, &b, sizeof(a)), "");
	zassert_equal(32, bit_difference(&a, &b, sizeof(a)), "");
	b = 0xAAAAAAAA;
	zassert_equal(0, bit_difference(&b, &b, sizeof(a)), "");
	zassert_equal(16, bit_difference(&a, &b, sizeof(a)), "");
	a = 0xFFFF0000;
	b = 0x0000FFFF;
	zassert_equal(32, bit_difference(&a, &b, sizeof(a)), "");
	a = 0xFFFFFF00;
	b = 0x00FFFFFF;
	zassert_equal(16, bit_difference(&a, &b, sizeof(a)), "");
}

ZTEST(epacket_keys, test_network_ids)
{
	zassert_equal(0x000000, infuse_security_network_key_identifier());
	zassert_equal(0xFFFFFF, infuse_security_secondary_network_key_identifier());
}

ZTEST(epacket_keys, test_invalid_key)
{
	const char *const info = "test";
	psa_key_id_t id;

	zassert_equal(-EINVAL, epacket_key_derive(PSA_KEY_ID_NULL, info, strlen(info), 1, &id));
}

ZTEST(epacket_keys, test_key_derive)
{
	uint8_t key_1[KEY_SIZE];
	uint8_t key_2[KEY_SIZE];
	const char *info, *info2, *info3;
	psa_key_id_t id_1, id_2;
	uint32_t rotation;
	int rc1, rc2;

	info = "test";
	info2 = "tess";
	info3 = "testt";
	rotation = 1;

	/* Same inputs give same key */
	rc1 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info), rotation,
				 &id_1);
	rc2 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info), rotation,
				 &id_2);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_equal(0, epacket_key_export(id_1, key_1), "Export failed");
	zassert_equal(0, epacket_key_export(id_2, key_2), "Export failed");
	zassert_equal(0, epacket_key_delete(id_1), "Delete failed");
	zassert_equal(0, epacket_key_delete(id_2), "Delete failed");
	zassert_equal(0, bit_difference(key_1, key_2, KEY_SIZE), "Derivation not deterministic");

	/* Base change gives different keys */
	rc1 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info), rotation,
				 &id_1);
	rc2 = epacket_key_derive(infuse_security_network_root_key(), info, strlen(info), rotation,
				 &id_2);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_equal(0, epacket_key_export(id_1, key_1), "Export failed");
	zassert_equal(0, epacket_key_export(id_2, key_2), "Export failed");
	zassert_equal(0, epacket_key_delete(id_1), "Delete failed");
	zassert_equal(0, epacket_key_delete(id_2), "Delete failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");

	/* Rotation gives different keys */
	for (int i = 1; i < 100; i++) {
		rc1 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info),
					 rotation, &id_1);
		rc2 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info),
					 rotation + i, &id_2);

		zassert_equal(0, rc1, "Derivation failed");
		zassert_equal(0, rc2, "Derivation failed");
		zassert_equal(0, epacket_key_export(id_1, key_1), "Export failed");
		zassert_equal(0, epacket_key_export(id_2, key_2), "Export failed");
		zassert_equal(0, epacket_key_delete(id_1), "Delete failed");
		zassert_equal(0, epacket_key_delete(id_2), "Delete failed");
		zassert_true(keys_different(key_1, key_2), "Keys too similar");
	}

	/* Info change gives different keys */
	rc1 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info), rotation,
				 &id_1);
	rc2 = epacket_key_derive(infuse_security_device_root_key(), info2, strlen(info2), rotation,
				 &id_2);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_equal(0, epacket_key_export(id_1, key_1), "Export failed");
	zassert_equal(0, epacket_key_export(id_2, key_2), "Export failed");
	zassert_equal(0, epacket_key_delete(id_1), "Delete failed");
	zassert_equal(0, epacket_key_delete(id_2), "Delete failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");

	rc1 = epacket_key_derive(infuse_security_device_root_key(), info, strlen(info), rotation,
				 &id_1);
	rc2 = epacket_key_derive(infuse_security_device_root_key(), info3, strlen(info3), rotation,
				 &id_2);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_equal(0, epacket_key_export(id_1, key_1), "Export failed");
	zassert_equal(0, epacket_key_export(id_2, key_2), "Export failed");
	zassert_equal(0, epacket_key_delete(id_1), "Delete failed");
	zassert_equal(0, epacket_key_delete(id_2), "Delete failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");
}

ZTEST(epacket_keys, test_key_id_get)
{
	psa_key_id_t id_1, id_2;

	/* Invalid interface ID */
	id_1 = epacket_key_id_get(EPACKET_KEY_INTERFACE_NUM,
				  infuse_security_network_key_identifier(), 1);
	zassert_equal(PSA_KEY_ID_NULL, id_1);

	/* We expect rotations of the same interface key to have the same ID */
	id_1 = epacket_key_id_get(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_device_key_identifier(), 1);
	id_2 = epacket_key_id_get(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_device_key_identifier(), 1);
	zassert_equal(id_1, id_2);

	id_2 = epacket_key_id_get(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_device_key_identifier(), 2);
	zassert_equal(id_1, id_2);

	/* Device and network keys should have different IDs*/
	id_2 = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_network_key_identifier(), 1);
	zassert_not_equal(id_1, id_2);

	/* Different interface keys should have different IDs */
	id_2 = epacket_key_id_get(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_UDP,
				  infuse_security_device_key_identifier(), 1);
	zassert_not_equal(id_1, id_2);

	/* Priamry and secondary networks should results in different keys */
	id_1 = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_network_key_identifier(), 1);
	id_2 = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_secondary_network_key_identifier(), 1);
	zassert_not_equal(PSA_KEY_ID_NULL, id_1);
	zassert_not_equal(PSA_KEY_ID_NULL, id_2);
	zassert_not_equal(id_1, id_2);

	/* Keys not matching the default ID's should fails */
	id_1 = epacket_key_id_get(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_device_key_identifier() + 1, 1);
	zassert_equal(PSA_KEY_ID_NULL, id_1);
	id_1 = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL,
				  infuse_security_network_key_identifier() + 1, 1);
	zassert_equal(PSA_KEY_ID_NULL, id_1);
}

ZTEST(epacket_keys, test_extension_networks)
{
	psa_key_attributes_t key_attributes = infuse_security_hkdf_attributes();
	uint8_t key_value[32];
	psa_key_id_t key_ids[CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS + 1];
	psa_key_id_t out_key, prev_key = PSA_KEY_ID_NULL;
	psa_status_t status;
	int rc;

	zassert_equal(-EINVAL, epacket_key_extension_network_add(PSA_KEY_ID_NULL, 0));

	/* Create the base keys */
	for (int i = 0; i < ARRAY_SIZE(key_ids); i++) {
		sys_rand_get(key_value, sizeof(key_value));
		status = psa_import_key(&key_attributes, key_value, sizeof(key_value), &key_ids[i]);
		zassert_equal(PSA_SUCCESS, status);
		zassert_not_equal(PSA_KEY_ID_NULL, key_ids[i]);
	}

	/* Add the networks to the key manager */
	for (int i = 0; i < CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS; i++) {
		rc = epacket_key_extension_network_add(key_ids[i], 1000 + i);
		zassert_equal(0, rc);
		rc = epacket_key_extension_network_add(key_ids[i], 1000 + i);
		zassert_equal(-EALREADY, rc);
	}
	/* Adding one more than there is space for */
	rc = epacket_key_extension_network_add(key_ids[ARRAY_SIZE(key_ids) - 1], 999);
	zassert_equal(-ENOMEM, rc);

	/* Ensure they can be retrieved and are not the same as previous keys */
	for (int i = 0; i < CONFIG_EPACKET_KEYS_EXTENSION_NETWORKS; i++) {
		for (int j = 0; j < EPACKET_KEY_INTERFACE_NUM; j++) {
			out_key = epacket_key_id_get(EPACKET_KEY_NETWORK | j, 1000 + i, 123456);
			zassert_not_equal(PSA_KEY_ID_NULL, out_key);
			zassert_true(out_key > prev_key);
			prev_key = out_key;
		}
	}
	/* Retrieving same key returns same key */
	prev_key = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1000,
				      123456);
	out_key = epacket_key_id_get(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1000,
				     123456);
	zassert_equal(prev_key, out_key);
}

static bool security_init(const void *global_state)
{
	infuse_security_init();
	return true;
}

ZTEST_SUITE(epacket_keys, security_init, NULL, NULL, NULL, NULL);
