/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/random/random.h>

#include <infuse/crypto/hardware_unique_key.h>

#include "nrf_cc3xx_platform_kmu.h"
#include "nrfx_nvmc.h"
#include "nrfx.h"

#if defined(CONFIG_HAS_HW_NRF_CC310)
#define HUK_SIZE_WORDS 4
#elif defined(CONFIG_HAS_HW_NRF_CC312)
#define HUK_SIZE_WORDS 8
#else
#error "This library requires CryptoCell"
#endif

#define HUK_SIZE_BYTES (HUK_SIZE_WORDS * 4)

static psa_key_id_t huk_key_id;

bool kmu_slot_written(uint32_t idx)
{
	bool written = false;

	/* Key slots are 1 indexed */
	NRF_KMU->SELECTKEYSLOT = idx + 1;
	if (nrfx_nvmc_uicr_word_read(&NRF_UICR_S->KEYSLOT.CONFIG[idx].PERM) != 0xFFFFFFFF) {
		written = true;
		goto end;
	}
	if (nrfx_nvmc_uicr_word_read(&NRF_UICR_S->KEYSLOT.CONFIG[idx].DEST) != 0xFFFFFFFF) {
		written = true;
		goto end;
	}
	for (int i = 0; i < 4; i++) {
		if (nrfx_nvmc_uicr_word_read(&NRF_UICR_S->KEYSLOT.KEY[idx].VALUE[i]) !=
		    0xFFFFFFFF) {
			written = true;
			goto end;
		}
	}

end:
	NRF_KMU->SELECTKEYSLOT = 0;
	return written;
}

int hardware_unique_key_init(void)
{
	uint8_t key[MAX(HUK_SIZE_BYTES, 32)];
	int rc = 0;

	/* Ensure HUK exists in KMU */
	if (!kmu_slot_written(0)) {
		/* Cryptographically secure random key */
		rc = sys_csrand_get(key, HUK_SIZE_BYTES);
		if (rc != 0) {
			return rc;
		}

		/* Write key to KMU */
		rc = nrf_cc3xx_platform_kmu_write_key_slot(
			0, NRF_CC3XX_PLATFORM_KMU_AES_ADDR,
			NRF_CC3XX_PLATFORM_KMU_DEFAULT_PERMISSIONS, key);
#if defined(CONFIG_HAS_HW_NRF_CC312)
		/* 2 part key required for CC312 */
		if (rc == 0) {
			rc = nrf_cc3xx_platform_kmu_write_key_slot(
				1, NRF_CC3XX_PLATFORM_KMU_AES_ADDR_2,
				NRF_CC3XX_PLATFORM_KMU_DEFAULT_PERMISSIONS,
				key + (HUK_SIZE_BYTES / 2));
		}
#endif /* CONFIG_HAS_HW_NRF_CC312 */

		/* Clear sensitive memory buffer */
		memset(key, 0x00, HUK_SIZE_BYTES);
	}

	/* Derive our HUK root from the KMU HUK */
	const char *const label = "INFUSE_HUK";
	const char *const context = "CTX";

	rc = nrf_cc3xx_platform_kmu_shadow_key_derive(0, HUK_SIZE_BYTES * 8, label, strlen(label),
						      context, strlen(context), key, 32);
	if (rc != 0) {
		return PSA_KEY_ID_NULL;
	}

	/* Push derived key into PSA */
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 256);

	status = psa_import_key(&key_attributes, key, 32, &huk_key_id);
	memset(key, 0x00, 32);
	return status == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t hardware_unique_key_id(void)
{
	return huk_key_id;
}
