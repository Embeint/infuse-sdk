/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/epacket/keys.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define ECDH_PUBLIC_KEY_SIZE 32

static const uint8_t m_pub_key_cloud[ECDH_PUBLIC_KEY_SIZE] = {
	0xc2, 0xfc, 0x16, 0x76, 0xa5, 0xda, 0xf5, 0x38, 0x8e, 0x64, 0x26,
	0x99, 0x83, 0xbf, 0xa6, 0x28, 0xfd, 0x9b, 0xf0, 0x94, 0xca, 0x51,
	0x58, 0x78, 0xec, 0x8f, 0xdb, 0xdb, 0x94, 0xb6, 0x3b, 0x44};
static uint8_t shared_secret[32];

static void create_device_keypair(psa_key_id_t *key_id)
{
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	/* ECDH, Curve25519 */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDH);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
	psa_set_key_bits(&key_attributes, 255);

	status = psa_generate_key(&key_attributes, key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to generate key pair! (%d)", status);
		return;
	}
}

int main(void)
{
	static uint8_t m_pub_key_device[ECDH_PUBLIC_KEY_SIZE];
	psa_key_id_t device_keypair;
	psa_status_t status;
	size_t olen;

	/* Create device private/public key */
	create_device_keypair(&device_keypair);

	/* Export the device public key */
	status = psa_export_public_key(device_keypair, m_pub_key_device, sizeof(m_pub_key_device),
				       &olen);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to export public key! (%d)", status);
		return -EINVAL;
	}
	printk("Device public key:\n\t");
	for (int i = 0; i < olen; i++) {
		printk("%02x", m_pub_key_device[i]);
	}
	printk("\n");

	/* Calculate shared secret using only cloud public key */
	status = psa_raw_key_agreement(PSA_ALG_ECDH, device_keypair, m_pub_key_cloud,
				       sizeof(m_pub_key_cloud), shared_secret,
				       sizeof(shared_secret), &olen);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_raw_key_agreement failed! (Error: %d)", status);
		return -EINVAL;
	}
	printk("Shared secret:\n\t");
	for (int i = 0; i < olen; i++) {
		printk("%02x", shared_secret[i]);
	}
	printk("\n");

	return 0;
}
