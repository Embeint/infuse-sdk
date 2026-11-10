/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/devicetree.h>
#include <zephyr/random/random.h>

#include <infuse/crypto/hardware_unique_key.h>
#include <psa/crypto.h>

#include <cracen/lib_kmu.h>

static psa_key_id_t huk_key_id;

#define KMU_PUSH_AREA_SIZE 32

#if DT_NODE_EXISTS(DT_NODELABEL(kmu_push_area))
/* The KMU push area is reserved at the start of SRAM by devicetree. Slots
 * provisioned by earlier releases push to this address, so it must not move.
 */
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(kmu_push_area)) >= KMU_PUSH_AREA_SIZE);
static uint8_t *const kmu_push_area = (uint8_t *)DT_REG_ADDR(DT_NODELABEL(kmu_push_area));
#else
uint8_t kmu_push_area[KMU_PUSH_AREA_SIZE] __attribute__((section(".nrf_kmu_reserved_push_area")));
#endif

static bool kmu_push_area_empty(void)
{
	uint8_t acc = 0;

	for (int i = 0; i < KMU_PUSH_AREA_SIZE; i++) {
		acc |= kmu_push_area[i];
	}
	return acc == 0;
}

int hardware_unique_key_init(void)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	struct kmu_src kmu_src_info = {0};
	psa_status_t status;
	int rc = 0;

	/* Infuse HUK PSA attributes */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HKDF(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_DERIVE);
	psa_set_key_bits(&key_attributes, 256);

	/* Ensure HUK exists in KMU */
	if (lib_kmu_is_slot_empty(0)) {
		/* Infuse-IoT HUK can rotate, no metadata needed */
		kmu_src_info.metadata = 0;
		kmu_src_info.rpolicy = LIB_KMU_REV_POLICY_ROTATING;

		/* 256 bit key needs 2 key slots */
		for (int i = 0; i < 2; i++) {
			/* Cryptographically secure random data */
			rc = sys_csrand_get(kmu_src_info.value, sizeof(kmu_src_info.value));
			if (rc != 0) {
				return rc;
			}
			/* Pushed to a continuous memory region */
			kmu_src_info.dest = (uintptr_t)kmu_push_area + (16 * i);
			/* Push the HUK to the KMU */
			rc = lib_kmu_provision_slot(i, &kmu_src_info);
			if (rc != LIB_KMU_SUCCESS) {
				return -EIO;
			}
		}
	}

	/* Push key data to kmu_push_area */
	memset(kmu_push_area, 0x00, KMU_PUSH_AREA_SIZE);
	rc = lib_kmu_push_slot(0);
	if (rc != LIB_KMU_SUCCESS) {
		return -EIO;
	}
	rc = lib_kmu_push_slot(1);
	if (rc != LIB_KMU_SUCCESS) {
		return -EIO;
	}
	/* Slots provisioned with a different destination push elsewhere */
	if (kmu_push_area_empty()) {
		return -EIO;
	}

	/* Import the key into PSA */
	status = psa_import_key(&key_attributes, kmu_push_area, KMU_PUSH_AREA_SIZE, &huk_key_id);
	memset(kmu_push_area, 0x00, KMU_PUSH_AREA_SIZE);
	return status == PSA_SUCCESS ? 0 : -EINVAL;
}

psa_key_id_t hardware_unique_key_id(void)
{
	return huk_key_id;
}
