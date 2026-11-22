/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_TESTS_LIB_CRYPTO_PSA_VECTORS_H_
#define INFUSE_TESTS_LIB_CRYPTO_PSA_VECTORS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <psa/crypto.h>

/* NIST CAVP SHAVS vector */
struct hash_vector {
	const uint8_t *msg;
	size_t msg_len;
	const uint8_t *md;
};

enum mct_type {
	/* SHAVS: MD[i] = SHA(MD[i-3] || MD[i-2] || MD[i-1]) */
	MCT_SHA2,
	/* SHA3VS: MD[i] = SHA3(MD[i-1]) */
	MCT_SHA3,
};

struct hash_suite {
	const char *name;
	psa_algorithm_t alg;
	const struct hash_vector *vectors;
	size_t num_vectors;
	enum mct_type mct_type;
	const uint8_t *mct_seed;
	size_t mct_seed_len;
	/* Concatenated digests after each 1000 iterations */
	const uint8_t *mct_checkpoints;
	size_t mct_num_checkpoints;
};

/* RFC 8439 or Project Wycheproof vector (256 bit key, 96 bit nonce, 128 bit tag) */
struct aead_vector {
	const uint8_t *key;
	const uint8_t *nonce;
	const uint8_t *aad;
	size_t aad_len;
	const uint8_t *msg;
	size_t msg_len;
	/* Ciphertext followed by the tag (msg_len + 16 bytes) */
	const uint8_t *ct_tag;
	/* Tag is expected to validate */
	bool valid;
};

/* HMAC input (RFC 4231 test case inputs, plus longer data) */
struct mac_input {
	const uint8_t *key;
	size_t key_len;
	const uint8_t *data;
	size_t data_len;
};

struct mac_suite {
	const char *name;
	psa_algorithm_t alg;
	/* Concatenated MACs of each mac_inputs entry (PSA_MAC_LENGTH bytes each) */
	const uint8_t *macs;
};

/* HKDF input (RFC 5869 test case inputs, plus a salt larger than the block size) */
struct kdf_input {
	const uint8_t *ikm;
	size_t ikm_len;
	const uint8_t *salt;
	size_t salt_len;
	const uint8_t *info;
	size_t info_len;
	size_t okm_len;
};

struct kdf_suite {
	const char *name;
	psa_algorithm_t alg;
	/* Concatenated output keying material of each kdf_inputs entry (okm_len bytes each) */
	const uint8_t *okms;
};

extern const struct hash_suite hash_suites[];
extern const size_t hash_suites_num;

extern const struct aead_vector chachapoly_vectors[];
extern const size_t chachapoly_vectors_num;

extern const struct mac_input mac_inputs[];
extern const size_t mac_inputs_num;
extern const struct mac_suite mac_suites[];
extern const size_t mac_suites_num;

extern const struct kdf_input kdf_inputs[];
extern const size_t kdf_inputs_num;
extern const struct kdf_suite kdf_suites[];
extern const size_t kdf_suites_num;

#endif /* INFUSE_TESTS_LIB_CRYPTO_PSA_VECTORS_H_ */
