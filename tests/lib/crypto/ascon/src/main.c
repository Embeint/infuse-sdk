/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/crypto/ascon.h>

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
	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(key, sizeof(key));
	sys_rand_get(associated_data, sizeof(associated_data));
	sys_rand_get(nonce, sizeof(nonce));
}

ZTEST(ascon, test_ascon128)
{
	unsigned long long clen;
	unsigned long long mlen;
	int rc = 0;

	buffers_init();

	for (int size = 1; size < 128; size++) {
		/* Encrypt the payload */
		rc = ascon128_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data,
					   sizeof(associated_data), tag, nonce, key);
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

ZTEST(ascon, test_ascon128_associated_data)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	/* No associated data */
	rc = ascon128_aead_encrypt(ciphertext, &clen, plaintext, size, NULL, 0, tag, nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, NULL, 0, nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");

	/* Small associated data */
	rc = ascon128_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data, 4, tag,
				   nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4,
				   nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
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
		rc = ascon128_aead_encrypt(ciphertext + i, &clen, plaintext + i, size,
					   associated_data + i, sizeof(associated_data), tag + i,
					   nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon128_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen,
					   associated_data + i, sizeof(associated_data), nonce + i,
					   key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext + i, decrypted + i, size,
				  "Decrypted does not equal input");
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
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		tag[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		tag[0]--;

		/* Key error */
		key[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon128a_associated_data)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	/* No associated data */
	rc = ascon128a_aead_encrypt(ciphertext, &clen, plaintext, size, NULL, 0, tag, nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, NULL, 0, nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");

	/* Small associated data */
	rc = ascon128a_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data, 4, tag,
				    nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4,
				    nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
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
		rc = ascon128a_aead_encrypt(ciphertext + i, &clen, plaintext + i, size,
					    associated_data + i, sizeof(associated_data), tag + i,
					    nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon128a_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen,
					    associated_data + i, sizeof(associated_data), nonce + i,
					    key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext + i, decrypted + i, size,
				  "Decrypted does not equal input");
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
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		ciphertext[0]--;

		/* Tag error */
		tag[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		tag[0]--;

		/* Key error */
		key[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		key[0]--;

		/* Associated data error */
		associated_data[0]++;
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(-1, rc, "Decryption did not fail");
		associated_data[0]--;

		/* Decrypt the payload */
		rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
					    associated_data, sizeof(associated_data), nonce, key);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
	}
}

ZTEST(ascon, test_ascon80pq_associated_data)
{
	unsigned long long clen;
	unsigned long long mlen;
	int size = 64;
	int rc = 0;

	buffers_init();

	/* No associated data */
	rc = ascon80pq_aead_encrypt(ciphertext, &clen, plaintext, size, NULL, 0, tag, nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, NULL, 0, nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");

	/* Small associated data */
	rc = ascon80pq_aead_encrypt(ciphertext, &clen, plaintext, size, associated_data, 4, tag,
				    nonce, key);
	zassert_equal(0, rc, "Encryption failed");
	zassert_equal(size, clen, "Unexpected ciphertext length");
	rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4,
				    nonce, key);
	zassert_equal(0, rc, "Decryption failed");
	zassert_equal(size, mlen, "Unexpected decrypt length");
	zassert_mem_equal(plaintext, decrypted, size, "Decrypted does not equal input");
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
		rc = ascon80pq_aead_encrypt(ciphertext + i, &clen, plaintext + i, size,
					    associated_data + i, sizeof(associated_data), tag + i,
					    nonce + i, key + i);
		zassert_equal(0, rc, "Encryption failed");
		zassert_equal(size, clen, "Unexpected ciphertext length");

		/* Decrypt the payload */
		rc = ascon80pq_aead_decrypt(decrypted + i, &mlen, tag + i, ciphertext + i, clen,
					    associated_data + i, sizeof(associated_data), nonce + i,
					    key + i);
		zassert_equal(0, rc, "Decryption failed");
		zassert_equal(size, mlen, "Unexpected decrypt length");
		zassert_mem_equal(plaintext + i, decrypted + i, size,
				  "Decrypted does not equal input");
	}
}

ZTEST_SUITE(ascon, NULL, NULL, NULL, NULL, NULL);
