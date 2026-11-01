/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <psa/crypto.h>

#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR
#include "ocrypto_chacha20_poly1305.h"
#include "ocrypto_curve25519.h"
#include "ocrypto_sha1.h"
#include "ocrypto_sha224.h"
#include "ocrypto_sha256.h"
#include "ocrypto_sha384.h"
#include "ocrypto_sha512.h"
#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR */

#define ACCEL_AEAD IS_ENABLED(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR)
#define ACCEL_HASH IS_ENABLED(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_SHA)
#define ACCEL_KA   IS_ENABLED(CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR_KEY_AGREEMENT)

/* Number of calls into the Oberon library, counted through linker wrapping */
static uint32_t oberon_aead_calls;
static uint32_t oberon_hash_calls;
static uint32_t oberon_ka_calls;

#ifdef CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR

void __real_ocrypto_chacha20_poly1305_encrypt(uint8_t *tag, uint8_t *c, const uint8_t *m,
					      size_t m_len, const uint8_t *a, size_t a_len,
					      const uint8_t *n, size_t n_len, const uint8_t *k);
int __real_ocrypto_chacha20_poly1305_decrypt(const uint8_t *tag, uint8_t *m, const uint8_t *c,
					     size_t c_len, const uint8_t *a, size_t a_len,
					     const uint8_t *n, size_t n_len, const uint8_t *k);
void __real_ocrypto_curve25519_scalarmult(uint8_t *r, const uint8_t *n, const uint8_t *p);

void __wrap_ocrypto_chacha20_poly1305_encrypt(uint8_t *tag, uint8_t *c, const uint8_t *m,
					      size_t m_len, const uint8_t *a, size_t a_len,
					      const uint8_t *n, size_t n_len, const uint8_t *k)
{
	oberon_aead_calls++;
	__real_ocrypto_chacha20_poly1305_encrypt(tag, c, m, m_len, a, a_len, n, n_len, k);
}

int __wrap_ocrypto_chacha20_poly1305_decrypt(const uint8_t *tag, uint8_t *m, const uint8_t *c,
					     size_t c_len, const uint8_t *a, size_t a_len,
					     const uint8_t *n, size_t n_len, const uint8_t *k)
{
	oberon_aead_calls++;
	return __real_ocrypto_chacha20_poly1305_decrypt(tag, m, c, c_len, a, a_len, n, n_len, k);
}

void __wrap_ocrypto_curve25519_scalarmult(uint8_t *r, const uint8_t *n, const uint8_t *p)
{
	oberon_ka_calls++;
	__real_ocrypto_curve25519_scalarmult(r, n, p);
}

#define HASH_WRAPPERS(name)                                                                        \
	void __real_ocrypto_##name(uint8_t *r, const uint8_t *in, size_t in_len);                  \
	void __real_ocrypto_##name##_final(ocrypto_##name##_ctx *ctx, uint8_t *r);                 \
	void __wrap_ocrypto_##name(uint8_t *r, const uint8_t *in, size_t in_len)                   \
	{                                                                                          \
		oberon_hash_calls++;                                                               \
		__real_ocrypto_##name(r, in, in_len);                                              \
	}                                                                                          \
	void __wrap_ocrypto_##name##_final(ocrypto_##name##_ctx *ctx, uint8_t *r)                  \
	{                                                                                          \
		oberon_hash_calls++;                                                               \
		__real_ocrypto_##name##_final(ctx, r);                                             \
	}

HASH_WRAPPERS(sha1)
HASH_WRAPPERS(sha224)
HASH_WRAPPERS(sha256)
HASH_WRAPPERS(sha384)
HASH_WRAPPERS(sha512)

#endif /* CONFIG_TF_PSA_CRYPTO_OBERON_ACCELERATOR */

static void expect_routing(uint32_t calls, uint32_t calls_before, bool accelerated)
{
	if (accelerated) {
		zassert_true(calls > calls_before, "Operation not handled by Oberon");
	} else {
		zassert_equal(calls_before, calls, "Operation unexpectedly handled by Oberon");
	}
}

/* RFC 8439, Section 2.8.2 */
static const uint8_t chachapoly_key[32] = {
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
	0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
	0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};
static const uint8_t chachapoly_nonce[12] = {
	0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
};
static const uint8_t chachapoly_aad[12] = {
	0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
};
static const char chachapoly_plaintext[] =
	"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
	"future, sunscreen would be it.";
/* Ciphertext followed by the 16 byte tag */
static const uint8_t chachapoly_ciphertext[] = {
	0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e,
	0xc2, 0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe, 0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee,
	0x62, 0xd6, 0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda,
	0x92, 0x72, 0x8b, 0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6,
	0x7e, 0xcd, 0x3b, 0x36, 0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c, 0x98, 0x03, 0xae,
	0xe3, 0x28, 0x09, 0x1b, 0x58, 0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85,
	0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc, 0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5,
	0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b, 0x61, 0x16, 0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09,
	0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91,
};

ZTEST(oberon, test_chacha20_poly1305)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	const size_t pt_len = sizeof(chachapoly_plaintext) - 1;
	uint8_t ciphertext[sizeof(chachapoly_ciphertext)];
	uint8_t decrypted[sizeof(chachapoly_plaintext)];
	uint32_t calls;
	psa_key_id_t key;
	size_t out_len;

	zassert_equal(sizeof(chachapoly_ciphertext), pt_len + 16);

	psa_set_key_type(&attr, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&attr, 256);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_CHACHA20_POLY1305);
	zassert_equal(PSA_SUCCESS,
		      psa_import_key(&attr, chachapoly_key, sizeof(chachapoly_key), &key));

	/* Encrypt matches the test vector */
	calls = oberon_aead_calls;
	zassert_equal(PSA_SUCCESS,
		      psa_aead_encrypt(key, PSA_ALG_CHACHA20_POLY1305, chachapoly_nonce,
				       sizeof(chachapoly_nonce), chachapoly_aad,
				       sizeof(chachapoly_aad), chachapoly_plaintext, pt_len,
				       ciphertext, sizeof(ciphertext), &out_len));
	zassert_equal(sizeof(chachapoly_ciphertext), out_len);
	zassert_mem_equal(chachapoly_ciphertext, ciphertext, out_len);
	expect_routing(oberon_aead_calls, calls, ACCEL_AEAD);

	/* Decrypt recovers the plaintext */
	calls = oberon_aead_calls;
	zassert_equal(PSA_SUCCESS,
		      psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, chachapoly_nonce,
				       sizeof(chachapoly_nonce), chachapoly_aad,
				       sizeof(chachapoly_aad), chachapoly_ciphertext,
				       sizeof(chachapoly_ciphertext), decrypted, sizeof(decrypted),
				       &out_len));
	zassert_equal(pt_len, out_len);
	zassert_mem_equal(chachapoly_plaintext, decrypted, out_len);
	expect_routing(oberon_aead_calls, calls, ACCEL_AEAD);

	/* Modified ciphertext, tag and associated data are all rejected */
	for (int i = 0; i < 3; i++) {
		uint8_t aad[sizeof(chachapoly_aad)];

		memcpy(ciphertext, chachapoly_ciphertext, sizeof(ciphertext));
		memcpy(aad, chachapoly_aad, sizeof(aad));
		if (i == 0) {
			ciphertext[0] ^= 0x01;
		} else if (i == 1) {
			ciphertext[sizeof(ciphertext) - 1] ^= 0x01;
		} else {
			aad[0] ^= 0x01;
		}
		zassert_equal(PSA_ERROR_INVALID_SIGNATURE,
			      psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, chachapoly_nonce,
					       sizeof(chachapoly_nonce), aad, sizeof(aad),
					       ciphertext, sizeof(ciphertext), decrypted,
					       sizeof(decrypted), &out_len));
	}

	/* Output buffers that are too small are rejected */
	zassert_not_equal(PSA_SUCCESS,
			  psa_aead_encrypt(key, PSA_ALG_CHACHA20_POLY1305, chachapoly_nonce,
					   sizeof(chachapoly_nonce), chachapoly_aad,
					   sizeof(chachapoly_aad), chachapoly_plaintext, pt_len,
					   ciphertext, sizeof(ciphertext) - 1, &out_len));
	zassert_not_equal(PSA_SUCCESS,
			  psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, chachapoly_nonce,
					   sizeof(chachapoly_nonce), chachapoly_aad,
					   sizeof(chachapoly_aad), chachapoly_ciphertext,
					   sizeof(chachapoly_ciphertext), decrypted, pt_len - 1,
					   &out_len));

	zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
}

struct hash_vector {
	psa_algorithm_t alg;
	size_t len;
	/* Hash of "abc" (FIPS 180-2) */
	uint8_t abc[PSA_HASH_MAX_SIZE];
};

static const struct hash_vector hash_vectors[] = {
	{
		.alg = PSA_ALG_SHA_1,
		.len = 20,
		.abc = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
			0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d},
	},
	{
		.alg = PSA_ALG_SHA_224,
		.len = 28,
		.abc = {0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22, 0x86, 0x42,
			0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3, 0x2a, 0xad, 0xbc, 0xe4,
			0xbd, 0xa0, 0xb3, 0xf7, 0xe3, 0x6c, 0x9d, 0xa7},
	},
	{
		.alg = PSA_ALG_SHA_256,
		.len = 32,
		.abc = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
			0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
			0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad},
	},
	{
		.alg = PSA_ALG_SHA_384,
		.len = 48,
		.abc = {0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b, 0xb5, 0xa0, 0x3d, 0x69,
			0x9a, 0xc6, 0x50, 0x07, 0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
			0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed, 0x80, 0x86, 0x07, 0x2b,
			0xa1, 0xe7, 0xcc, 0x23, 0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7},
	},
	{
		.alg = PSA_ALG_SHA_512,
		.len = 64,
		.abc = {0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73,
			0x49, 0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9,
			0x7e, 0xa2, 0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21,
			0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23,
			0xa3, 0xfe, 0xeb, 0xbd, 0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8,
			0x0e, 0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f},
	},
};

/* Static to keep the test thread stack usage low */
static psa_hash_operation_t op;
static psa_hash_operation_t clone;
static uint8_t long_input[300];

ZTEST(oberon, test_hash)
{
	uint8_t expected[PSA_HASH_MAX_SIZE];
	uint8_t hash[PSA_HASH_MAX_SIZE];
	size_t hash_len;
	uint32_t calls;

	for (int i = 0; i < sizeof(long_input); i++) {
		long_input[i] = i;
	}

	ARRAY_FOR_EACH_PTR(hash_vectors, v) {
		op = psa_hash_operation_init();
		clone = psa_hash_operation_init();

		/* Single-shot matches the test vector */
		calls = oberon_hash_calls;
		zassert_equal(PSA_SUCCESS,
			      psa_hash_compute(v->alg, "abc", 3, hash, sizeof(hash), &hash_len));
		zassert_equal(v->len, hash_len);
		zassert_mem_equal(v->abc, hash, hash_len);
		expect_routing(oberon_hash_calls, calls, ACCEL_HASH);

		/* Output buffer too small */
		zassert_not_equal(PSA_SUCCESS,
				  psa_hash_compute(v->alg, "abc", 3, hash, v->len - 1, &hash_len));

		/* Multi-part matches the test vector */
		calls = oberon_hash_calls;
		zassert_equal(PSA_SUCCESS, psa_hash_setup(&op, v->alg));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "a", 1));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "bc", 2));
		zassert_equal(PSA_SUCCESS, psa_hash_finish(&op, hash, sizeof(hash), &hash_len));
		zassert_equal(v->len, hash_len);
		zassert_mem_equal(v->abc, hash, hash_len);
		expect_routing(oberon_hash_calls, calls, ACCEL_HASH);

		/* Multi-part across block boundaries matches single-shot */
		zassert_equal(PSA_SUCCESS, psa_hash_compute(v->alg, long_input, sizeof(long_input),
							    expected, sizeof(expected), &hash_len));
		zassert_equal(PSA_SUCCESS, psa_hash_setup(&op, v->alg));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, long_input, 1));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, long_input + 1, 130));
		zassert_equal(PSA_SUCCESS,
			      psa_hash_update(&op, long_input + 131, sizeof(long_input) - 131));
		zassert_equal(PSA_SUCCESS, psa_hash_finish(&op, hash, sizeof(hash), &hash_len));
		zassert_mem_equal(expected, hash, hash_len);

		/* Cloned operations continue independently */
		zassert_equal(PSA_SUCCESS, psa_hash_setup(&op, v->alg));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "ab", 2));
		zassert_equal(PSA_SUCCESS, psa_hash_clone(&op, &clone));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "c", 1));
		zassert_equal(PSA_SUCCESS, psa_hash_finish(&op, hash, sizeof(hash), &hash_len));
		zassert_mem_equal(v->abc, hash, hash_len);
		zassert_equal(PSA_SUCCESS, psa_hash_update(&clone, "c", 1));
		zassert_equal(PSA_SUCCESS, psa_hash_finish(&clone, hash, sizeof(hash), &hash_len));
		zassert_mem_equal(v->abc, hash, hash_len);

		/* Aborted operations can be reused */
		zassert_equal(PSA_SUCCESS, psa_hash_setup(&op, v->alg));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "xyz", 3));
		zassert_equal(PSA_SUCCESS, psa_hash_abort(&op));
		zassert_equal(PSA_SUCCESS, psa_hash_setup(&op, v->alg));
		zassert_equal(PSA_SUCCESS, psa_hash_update(&op, "abc", 3));
		zassert_equal(PSA_SUCCESS, psa_hash_verify(&op, v->abc, v->len));
	}
}

/* RFC 7748, Section 6.1 */
static const uint8_t x25519_alice_private[32] = {
	0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1,
	0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0,
	0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a,
};
static const uint8_t x25519_bob_public[32] = {
	0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61,
	0xc2, 0xec, 0xe4, 0x35, 0x37, 0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78,
	0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f,
};
static const uint8_t x25519_shared[32] = {
	0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1, 0x72, 0x8e, 0x3b,
	0xf4, 0x80, 0x35, 0x0f, 0x25, 0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1,
	0x9e, 0x33, 0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42,
};

ZTEST(oberon, test_x25519)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	uint8_t shared[32];
	size_t shared_len;
	psa_key_id_t key;
	uint32_t calls;

	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
	psa_set_key_bits(&attr, 255);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDH);
	zassert_equal(PSA_SUCCESS, psa_import_key(&attr, x25519_alice_private,
						  sizeof(x25519_alice_private), &key));

	calls = oberon_ka_calls;
	zassert_equal(PSA_SUCCESS, psa_raw_key_agreement(PSA_ALG_ECDH, key, x25519_bob_public,
							 sizeof(x25519_bob_public), shared,
							 sizeof(shared), &shared_len));
	zassert_equal(sizeof(x25519_shared), shared_len);
	zassert_mem_equal(x25519_shared, shared, shared_len);
	expect_routing(oberon_ka_calls, calls, ACCEL_KA);

	/* Truncated peer key is rejected */
	zassert_not_equal(PSA_SUCCESS, psa_raw_key_agreement(PSA_ALG_ECDH, key, x25519_bob_public,
							     sizeof(x25519_bob_public) - 1, shared,
							     sizeof(shared), &shared_len));

	zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
}

static void *oberon_setup(void)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return NULL;
}

ZTEST_SUITE(oberon, NULL, oberon_setup, NULL, NULL, NULL);
