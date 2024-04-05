/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <eis/crypto/ascon.h>

#define ALIGNMENT 8

uint8_t plaintext[128 + ALIGNMENT];
uint8_t ciphertext[128 + ALIGNMENT];
uint8_t decrypted[128 + ALIGNMENT];
uint8_t associated_data[16 + ALIGNMENT];
uint8_t key[20 + ALIGNMENT];
uint8_t nonce[16 + ALIGNMENT];
uint8_t tag[16 + ALIGNMENT];

static void buffers_init(void)
{
	/* Generate some arbitrary data */
	for (int i = 0; i < 128; i++) {
		plaintext[i] = (2 * i) - 3;
	}
	for (int i = 0; i < 20; i++) {
		key[i] = (i - 1) * 5;
	}
	for (int i = 0; i < 16; i++) {
		associated_data[i] = (2 * i) - 3;
		nonce[i] = i + 1;
	}
}

ZTEST(ascon, test_ascon128)
{
	unsigned long long clen;
	unsigned long long mlen;
	int rc = 0;

	buffers_init();

	for (int size = 1; size < 128; size++) {
		/* Encrypt the payload */
		rc = ascon128_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data, sizeof(associated_data),
					   tag, nonce, key);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Ciphertext error */
		ciphertext[0]++;
		rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					   sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		tag[0]++;
		rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					   sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		tag[0]--;

		/* Key error */
		key[0]++;
		rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					   sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					   sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					   sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon128_unaligned)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	for (int i = 1; i < 8; i++) {
		/* Encrypt the payload */
		rc = ascon128_aead_encrypt(ciphertext + i, &clen, plaintext + i, size, associated_data + i,
					   sizeof(associated_data), tag + i, nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon128_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen, associated_data + i,
					   sizeof(associated_data), nonce + i, key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon128a)
{
	unsigned long long clen;
	unsigned long long mlen;
	int rc = 0;

	buffers_init();

	/* Reference implementation */
	for (int size = 1; size < 128; size++) {
		/* Encrypt the payload */
		rc = ascon128a_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data,
					    sizeof(associated_data), tag, nonce, key);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Ciphertext error */
		ciphertext[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		tag[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		tag[0]--;

		/* Key error */
		key[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon128a_unaligned)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	for (int i = 1; i < 8; i++) {
		/* Encrypt the payload */
		rc = ascon128a_aead_encrypt(ciphertext + i, &clen, plaintext + i, size, associated_data + i,
					    sizeof(associated_data), tag + i, nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon128a_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen, associated_data + i,
					    sizeof(associated_data), nonce + i, key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon80pq)
{
	unsigned long long clen;
	unsigned long long mlen;
	int rc = 0;

	buffers_init();

	/* Reference implementation */
	for (int size = 1; size < 128; size++) {
		/* Encrypt the payload */
		rc = ascon80pq_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data,
					    sizeof(associated_data), tag, nonce, key);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Ciphertext error */
		ciphertext[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		tag[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		tag[0]--;

		/* Key error */
		key[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data,
					    sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon80pq_unaligned)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	for (int i = 1; i < 8; i++) {
		/* Encrypt the payload */
		rc = ascon80pq_aead_encrypt(ciphertext + i, &clen, plaintext + i, size, associated_data + i,
					    sizeof(associated_data), tag + i, nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon80pq_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen, associated_data + i,
					    sizeof(associated_data), nonce + i, key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST_SUITE(ascon, NULL, NULL, NULL, NULL, NULL);
