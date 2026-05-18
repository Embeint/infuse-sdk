/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef __STDC_WANT_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1 /* Ask for the C11 memset_s() */
#endif

#include <string.h>

#include <zephyr/drivers/hwinfo.h>

#include <infuse/security.h>
#include <infuse/crypto/hardware_unique_key.h>
#include <psa/crypto.h>

static psa_key_id_t huk_key_id;

/* Picolibc supports memset_s but doesn't define __STDC_LIB_EXT1__ */
#if !defined(__STDC_LIB_EXT1__) && !defined(CONFIG_PICOLIBC)
static int memset_s(void *dest, size_t destsz, int ch, size_t count)
{
	ARG_UNUSED(destsz);

	memset(dest, ch, count);
	return 0;
}
#endif /* !defined(__STDC_LIB_EXT1__) && !defined(CONFIG_PICOLIBC) */

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
	memset_s(key, sizeof(key), 0x00, sizeof(key));
	return status == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t hardware_unique_key_id(void)
{
	return huk_key_id;
}
