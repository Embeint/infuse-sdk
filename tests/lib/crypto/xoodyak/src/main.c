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

#include <infuse/crypto/xoodyak.h>

#define ALIGNMENT 8

uint8_t plaintext[128 + ALIGNMENT];
uint8_t ciphertext[128 + 16 + ALIGNMENT];
uint8_t decrypted[128 + ALIGNMENT];
uint8_t associated_data[16 + ALIGNMENT];
uint8_t key[20 + ALIGNMENT];
uint8_t nonce[16 + ALIGNMENT];
uint8_t tag[16 + ALIGNMENT];

static void buffers_init(void)
{
	/* Generate some arbitrary data */
	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(key, sizeof(key));
	sys_rand_get(associated_data, sizeof(associated_data));
	sys_rand_get(nonce, sizeof(nonce));
}

ZTEST(xoodyak, test_xoodyak)
{
	unsigned long long clen;
	unsigned long long mlen;
	int rc = 0;

	buffers_init();

	for (int size = 1; size < 128; size++) {
		/* Encrypt the payload */
		rc = xoodyak_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data,
					  sizeof(associated_data), tag, nonce, key);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Ciphertext error */
		ciphertext[0]++;
		rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					  sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		ciphertext[clen - 1]++;
		rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					  sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[clen - 1]--;

		/* Key error */
		key[0]++;
		rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					  sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					  sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					  sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(xoodyak, test_xoodyak_associated_data)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	/* No associated data */
	rc = xoodyak_aead_encrypt(ciphertext, &clen, plaintext, size, tag, 0, tag, nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, tag, 0, nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");

	/* Small associated data */
	rc = xoodyak_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data, 4, tag,
				  nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4,
				  nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
}

ZTEST(xoodyak, test_xoodyak_unaligned)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	for (int i = 1; i < 8; i++) {
		/* Encrypt the payload */
		rc = xoodyak_aead_encrypt(ciphertext + i, &clen, plaintext + i, size,
					  associated_data + i, sizeof(associated_data), tag + i,
					  nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = xoodyak_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen,
					  associated_data + i, sizeof(associated_data), nonce + i,
					  key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext + i, decrypted + i, size,
				  "Decrypted does not equal input");
	}
}

ZTEST_SUITE(xoodyak, NULL, NULL, NULL, NULL, NULL);
