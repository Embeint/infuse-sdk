/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <eis/epacket/keys.h>

#define KEY_SIZE 16

uint8_t plaintext[1024];
uint8_t ciphertext1[1024];
uint8_t ciphertext2[1024];
uint8_t decrypted1[1024];
uint8_t decrypted2[1024];

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

/* For 128-bit keys the average binary difference should be 64 bits.
 * For validation purposes accept anything in the range 48 to 80.
 */
static bool keys_different(uint8_t *a, uint8_t *b)
{
	uint32_t difference = bit_difference(a, b, KEY_SIZE);

	return (difference >= 48) && (difference <= 80);
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

ZTEST(epacket_keys, test_key_derive)
{
	uint8_t key_1[KEY_SIZE];
	uint8_t key_2[KEY_SIZE];
	const char *info, *info2, *info3;
	uint32_t rotation;
	int rc1, rc2;

	info = "test";
	info2 = "tess";
	info3 = "testt";
	rotation = 1;

	/* Same inputs give same key */
	rc1 = epacket_key_derive(EPACKET_KEY_DEVICE, key_1, KEY_SIZE, info, strlen(info), rotation);
	rc2 = epacket_key_derive(EPACKET_KEY_DEVICE, key_2, KEY_SIZE, info, strlen(info), rotation);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_equal(0, bit_difference(key_1, key_2, KEY_SIZE), "Derivation not deterministic");

	/* Base change gives different keys */
	rc1 = epacket_key_derive(EPACKET_KEY_DEVICE, key_1, KEY_SIZE, info, strlen(info), rotation);
	rc2 = epacket_key_derive(EPACKET_KEY_NETWORK, key_2, KEY_SIZE, info, strlen(info), rotation);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");

	/* Rotation gives different keys */
	for (int i = 1; i < 100; i++) {
		rc1 = epacket_key_derive(EPACKET_KEY_DEVICE, key_1, KEY_SIZE, info, strlen(info), rotation);
		rc2 = epacket_key_derive(EPACKET_KEY_DEVICE, key_2, KEY_SIZE, info, strlen(info), rotation + i);

		zassert_equal(0, rc1, "Derivation failed");
		zassert_equal(0, rc2, "Derivation failed");
		zassert_true(keys_different(key_1, key_2), "Keys too similar");
	}

	/* Info change gives different keys */
	rc1 = epacket_key_derive(EPACKET_KEY_DEVICE, key_1, KEY_SIZE, info, strlen(info), rotation);
	rc2 = epacket_key_derive(EPACKET_KEY_DEVICE, key_2, KEY_SIZE, info2, strlen(info2), rotation);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");

	rc1 = epacket_key_derive(EPACKET_KEY_DEVICE, key_1, KEY_SIZE, info, strlen(info), rotation);
	rc2 = epacket_key_derive(EPACKET_KEY_DEVICE, key_2, KEY_SIZE, info3, strlen(info3), rotation);

	zassert_equal(0, rc1, "Derivation failed");
	zassert_equal(0, rc2, "Derivation failed");
	zassert_true(keys_different(key_1, key_2), "Keys too similar");
}

ZTEST(epacket_keys, test_encrypt_decrypt_params)
{
	uint8_t associated[4];
	uint8_t nonce[16];
	uint8_t tag1[16];
	uint8_t tag2[16];
	uint16_t len = 64;
	int rc1, rc2;

	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(associated, sizeof(associated));
	sys_rand_get(nonce, sizeof(nonce));

	/* Same inputs = same outputs */
	rc1 = epacket_encrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1, associated, sizeof(associated),
			      plaintext, len, nonce, tag1, ciphertext1);
	rc2 = epacket_encrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1, associated, sizeof(associated),
			      plaintext, len, nonce, tag2, ciphertext2);

	zassert_equal(0, rc1, "Encryption failed");
	zassert_equal(0, rc2, "Encryption failed");
	zassert_mem_equal(ciphertext1, ciphertext2, len, "Encryption not deterministic");

	/* Try decrypting with incorrect params */
	rc1 = epacket_decrypt(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL, 1, associated, sizeof(associated),
			      ciphertext1, len, nonce, tag1, decrypted1);
	zassert_equal(-1, rc1, "Decryption unexpectedly passed");
	rc1 = epacket_decrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1 + 1, associated, sizeof(associated),
			      ciphertext1, len, nonce, tag1, decrypted1);
	zassert_equal(-1, rc1, "Decryption unexpectedly passed");
	rc1 = epacket_decrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_UDP, 1, associated, sizeof(associated),
			      ciphertext1, len, nonce, tag1, decrypted1);
	zassert_equal(-1, rc1, "Decryption unexpectedly passed");
	rc1 = epacket_decrypt(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_UDP, 1, associated, sizeof(associated),
			      ciphertext1, len, nonce, tag1, decrypted1);
	zassert_equal(-1, rc1, "Decryption unexpectedly passed");

	/* Decrypt with correct params */
	rc1 = epacket_decrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, 1, associated, sizeof(associated),
			      ciphertext1, len, nonce, tag1, decrypted1);
	zassert_equal(0, rc1, "Decryption failed");
	zassert_mem_equal(plaintext, decrypted1, len, "Plaintext not recovered");
}

ZTEST(epacket_keys, test_encrypt_decrypt_length)
{
	const uint16_t payload_lengths[] = {1, 16, 64, 256, 1024};
	uint8_t associated[4];
	uint8_t nonce[16];
	uint8_t tag1[16];
	uint8_t tag2[16];
	uint16_t len = 64;
	int rc1, rc2;

	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(associated, sizeof(associated));
	sys_rand_get(nonce, sizeof(nonce));

	for (int i = 0; i < ARRAY_SIZE(payload_lengths); i++) {
		len = payload_lengths[i];

		rc1 = epacket_encrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, i, associated,
				      sizeof(associated), plaintext, len, nonce, tag1, ciphertext1);

		rc2 = epacket_decrypt(EPACKET_KEY_NETWORK | EPACKET_KEY_INTERFACE_SERIAL, i, associated,
				      sizeof(associated), ciphertext1, len, nonce, tag1, decrypted1);
		zassert_equal(0, rc1, "Encryption failed");
		zassert_equal(0, rc2, "Decryption failed");
		zassert_mem_equal(plaintext, decrypted1, len, "Plaintext not recovered");

		rc1 = epacket_encrypt(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL, i + 1, associated,
				      sizeof(associated), plaintext, len, nonce, tag2, ciphertext2);
		rc2 = epacket_decrypt(EPACKET_KEY_DEVICE | EPACKET_KEY_INTERFACE_SERIAL, i + 1, associated,
				      sizeof(associated), ciphertext2, len, nonce, tag2, decrypted2);
		zassert_equal(0, rc1, "Encryption failed");
		zassert_equal(0, rc2, "Decryption failed");
		zassert_mem_equal(plaintext, decrypted2, len, "Plaintext not recovered");
	}
}

ZTEST_SUITE(epacket_keys, NULL, NULL, NULL, NULL, NULL);
