/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/drivers/hwinfo.h>

#include <infuse/security.h>
#include <infuse/crypto/hardware_unique_key.h>
#include <psa/crypto.h>

static psa_key_id_t huk_key_id;

int hardware_unique_key_init(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t key[32] = {0};
	psa_status_t status;
	uint8_t hw_id[8];
	ssize_t hlen, rc;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 256);

	rc = hwinfo_get_device_id(hw_id, sizeof(hw_id));
	if (rc > 0) {
		status = psa_hash_compute(PSA_ALG_SHA_256, hw_id, rc, key, sizeof(key), &hlen);
		if (status != PSA_SUCCESS) {
			/* Default key value */
			memset(key, 0x42, sizeof(key));
		}
	}

	status = psa_import_key(&key_attributes, key, sizeof(key), &huk_key_id);
	memset(key, 0x00, sizeof(key));
	return status == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t hardware_unique_key_id(void)
{
	return huk_key_id;
}
