/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>

#include <psa/crypto.h>

#include "vectors.h"

#define CHACHAPOLY_KEY_LEN   32
#define CHACHAPOLY_NONCE_LEN 12
#define CHACHAPOLY_TAG_LEN   16
#define MAX_MSG_LEN          2048
#define MAX_AAD_LEN          1024
/* Maximum offset used to test unaligned buffers */
#define MAX_OFFSET           3

/* Where the inputs to an operation are located */
enum source {
	/* Vector used directly from flash (inaccessible to the CRACEN DMA) */
	SRC_FLASH,
	/* Copy of the vector in word aligned RAM */
	SRC_RAM,
	/* Copy of the vector in unaligned RAM */
	SRC_RAM_UNALIGNED,
	SRC_NUM,
};

static const char *const source_names[] = {
	[SRC_FLASH] = "flash",
	[SRC_RAM] = "RAM",
	[SRC_RAM_UNALIGNED] = "unaligned RAM",
};

static uint8_t ram_nonce[CHACHAPOLY_NONCE_LEN + MAX_OFFSET] __aligned(4);
static uint8_t ram_aad[MAX_AAD_LEN + MAX_OFFSET] __aligned(4);
static uint8_t ram_input[MAX_MSG_LEN + CHACHAPOLY_TAG_LEN + MAX_OFFSET] __aligned(4);
static uint8_t ram_output[MAX_MSG_LEN + CHACHAPOLY_TAG_LEN + MAX_OFFSET] __aligned(4);
static uint8_t tag[CHACHAPOLY_TAG_LEN];
static psa_hash_operation_t hash_op;
static psa_hash_operation_t hash_clone;
static psa_aead_operation_t aead_op;

/* Chunk sizes for multi-part operations, cycled through per vector */
static const uint16_t chunk_sizes[] = {1, 7, 16, 63, 64, 65, 128, 200, 1000};

static size_t source_offset(enum source src, size_t idx)
{
	return src == SRC_RAM_UNALIGNED ? 1 + (idx % MAX_OFFSET) : 0;
}

/* Return the input data from the requested source */
static const uint8_t *place(const uint8_t *data, size_t len, uint8_t *ram, size_t ram_size,
			    enum source src, size_t idx)
{
	size_t offset = source_offset(src, idx);

	if (src == SRC_FLASH) {
		return data;
	}
	zassert_true(offset + len <= ram_size, "RAM buffer too small (%zu)", len);
	memcpy(ram + offset, data, len);
	return ram + offset;
}

static psa_key_id_t chachapoly_key_import(const uint8_t *key)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&attr, PSA_ALG_CHACHA20_POLY1305);
	psa_set_key_type(&attr, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&attr, 8 * CHACHAPOLY_KEY_LEN);

	zassert_equal(PSA_SUCCESS, psa_import_key(&attr, key, CHACHAPOLY_KEY_LEN, &key_id));
	return key_id;
}

static void *psa_setup(void)
{
	zassert_equal(PSA_SUCCESS, psa_crypto_init());
	return NULL;
}

ZTEST(psa_chachapoly, test_encrypt)
{
	const uint8_t *nonce, *aad, *msg;
	uint8_t *output;
	psa_status_t status;
	psa_key_id_t key;
	size_t out_len;

	for (size_t i = 0; i < chachapoly_vectors_num; i++) {
		const struct aead_vector *v = &chachapoly_vectors[i];

		if (!v->valid) {
			continue;
		}
		key = chachapoly_key_import(v->key);

		for (enum source src = 0; src < SRC_NUM; src++) {
			nonce = place(v->nonce, CHACHAPOLY_NONCE_LEN, ram_nonce, sizeof(ram_nonce),
				      src, i);
			aad = place(v->aad, v->aad_len, ram_aad, sizeof(ram_aad), src, i);
			msg = place(v->msg, v->msg_len, ram_input, sizeof(ram_input), src, i);
			output = ram_output + source_offset(src, i);
			memset(ram_output, 0xAA, sizeof(ram_output));

			status = psa_aead_encrypt(key, PSA_ALG_CHACHA20_POLY1305, nonce,
						  CHACHAPOLY_NONCE_LEN, aad, v->aad_len, msg,
						  v->msg_len, output,
						  v->msg_len + CHACHAPOLY_TAG_LEN, &out_len);
			zassert_equal(PSA_SUCCESS, status, "Vector %zu (%s): status %d", i,
				      source_names[src], status);
			zassert_equal(v->msg_len + CHACHAPOLY_TAG_LEN, out_len);
			zassert_mem_equal(v->ct_tag, output, out_len, "Vector %zu (%s)", i,
					  source_names[src]);
		}
		zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
	}
}

ZTEST(psa_chachapoly, test_decrypt)
{
	const uint8_t *nonce, *aad, *ct_tag;
	uint8_t *output;
	psa_status_t status;
	psa_key_id_t key;
	size_t out_len;

	for (size_t i = 0; i < chachapoly_vectors_num; i++) {
		const struct aead_vector *v = &chachapoly_vectors[i];

		key = chachapoly_key_import(v->key);

		for (enum source src = 0; src < SRC_NUM; src++) {
			nonce = place(v->nonce, CHACHAPOLY_NONCE_LEN, ram_nonce, sizeof(ram_nonce),
				      src, i);
			aad = place(v->aad, v->aad_len, ram_aad, sizeof(ram_aad), src, i);
			ct_tag = place(v->ct_tag, v->msg_len + CHACHAPOLY_TAG_LEN, ram_input,
				       sizeof(ram_input), src, i);
			output = ram_output + source_offset(src, i);
			memset(ram_output, 0xAA, sizeof(ram_output));

			status = psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, nonce,
						  CHACHAPOLY_NONCE_LEN, aad, v->aad_len, ct_tag,
						  v->msg_len + CHACHAPOLY_TAG_LEN, output,
						  v->msg_len, &out_len);
			if (v->valid) {
				zassert_equal(PSA_SUCCESS, status, "Vector %zu (%s): status %d", i,
					      source_names[src], status);
				zassert_equal(v->msg_len, out_len);
				zassert_mem_equal(v->msg, output, out_len, "Vector %zu (%s)", i,
						  source_names[src]);
			} else {
				zassert_equal(PSA_ERROR_INVALID_SIGNATURE, status,
					      "Vector %zu (%s): status %d", i, source_names[src],
					      status);
			}
		}
		zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
	}
}

ZTEST(psa_chachapoly, test_in_place)
{
	psa_status_t status;
	psa_key_id_t key;
	size_t out_len;

	for (size_t i = 0; i < chachapoly_vectors_num; i++) {
		const struct aead_vector *v = &chachapoly_vectors[i];
		uint8_t *buf = ram_input + source_offset(SRC_RAM_UNALIGNED, i);

		key = chachapoly_key_import(v->key);

		if (v->valid) {
			/* Ciphertext overwrites the plaintext */
			memcpy(buf, v->msg, v->msg_len);
			status = psa_aead_encrypt(key, PSA_ALG_CHACHA20_POLY1305, v->nonce,
						  CHACHAPOLY_NONCE_LEN, v->aad, v->aad_len, buf,
						  v->msg_len, buf, v->msg_len + CHACHAPOLY_TAG_LEN,
						  &out_len);
			zassert_equal(PSA_SUCCESS, status, "Vector %zu: status %d", i, status);
			zassert_equal(v->msg_len + CHACHAPOLY_TAG_LEN, out_len);
			zassert_mem_equal(v->ct_tag, buf, out_len, "Vector %zu", i);
		}

		/* Plaintext overwrites the ciphertext */
		memcpy(buf, v->ct_tag, v->msg_len + CHACHAPOLY_TAG_LEN);
		status = psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, v->nonce,
					  CHACHAPOLY_NONCE_LEN, v->aad, v->aad_len, buf,
					  v->msg_len + CHACHAPOLY_TAG_LEN, buf, v->msg_len,
					  &out_len);
		if (v->valid) {
			zassert_equal(PSA_SUCCESS, status, "Vector %zu: status %d", i, status);
			zassert_equal(v->msg_len, out_len);
			zassert_mem_equal(v->msg, buf, out_len, "Vector %zu", i);
		} else {
			zassert_equal(PSA_ERROR_INVALID_SIGNATURE, status, "Vector %zu: status %d",
				      i, status);
		}
		zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
	}
}

ZTEST(psa_chachapoly, test_errors)
{
	const struct aead_vector *v = &chachapoly_vectors[0];
	psa_status_t status;
	psa_key_id_t key;
	size_t out_len;

	zassert_true(v->valid && (v->msg_len > 0));
	key = chachapoly_key_import(v->key);

	/* Output buffer too small for the ciphertext and tag */
	status = psa_aead_encrypt(key, PSA_ALG_CHACHA20_POLY1305, v->nonce, CHACHAPOLY_NONCE_LEN,
				  v->aad, v->aad_len, v->msg, v->msg_len, ram_output,
				  v->msg_len + CHACHAPOLY_TAG_LEN - 1, &out_len);
	zassert_equal(PSA_ERROR_BUFFER_TOO_SMALL, status, "status %d", status);

	/* Output buffer too small for the plaintext */
	status = psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, v->nonce, CHACHAPOLY_NONCE_LEN,
				  v->aad, v->aad_len, v->ct_tag, v->msg_len + CHACHAPOLY_TAG_LEN,
				  ram_output, v->msg_len - 1, &out_len);
	zassert_equal(PSA_ERROR_BUFFER_TOO_SMALL, status, "status %d", status);

	/* Ciphertext shorter than the tag */
	status = psa_aead_decrypt(key, PSA_ALG_CHACHA20_POLY1305, v->nonce, CHACHAPOLY_NONCE_LEN,
				  v->aad, v->aad_len, v->ct_tag, CHACHAPOLY_TAG_LEN - 1, ram_output,
				  sizeof(ram_output), &out_len);
	zassert_equal(PSA_ERROR_INVALID_ARGUMENT, status, "status %d", status);

	zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
}

static void aead_update_ad_chunked(const uint8_t *aad, size_t aad_len, size_t chunk)
{
	for (size_t pos = 0; pos < aad_len; pos += chunk) {
		zassert_equal(PSA_SUCCESS,
			      psa_aead_update_ad(&aead_op, aad + pos, MIN(chunk, aad_len - pos)));
	}
}

static size_t aead_update_chunked(const uint8_t *input, size_t len, uint8_t *output, size_t chunk)
{
	size_t out_pos = 0;
	size_t out_len;

	for (size_t pos = 0; pos < len; pos += chunk) {
		zassert_equal(PSA_SUCCESS,
			      psa_aead_update(&aead_op, input + pos, MIN(chunk, len - pos),
					      output + out_pos, len - out_pos, &out_len));
		out_pos += out_len;
	}
	return out_pos;
}

ZTEST(psa_chachapoly, test_multipart)
{
	psa_status_t status;
	psa_key_id_t key;
	size_t out_pos;
	size_t out_len;
	size_t tag_len;

	for (size_t i = 0; i < chachapoly_vectors_num; i++) {
		const struct aead_vector *v = &chachapoly_vectors[i];
		size_t chunk = chunk_sizes[i % ARRAY_SIZE(chunk_sizes)];

		key = chachapoly_key_import(v->key);

		if (v->valid) {
			aead_op = psa_aead_operation_init();
			status = psa_aead_encrypt_setup(&aead_op, key, PSA_ALG_CHACHA20_POLY1305);
			if (status == PSA_ERROR_NOT_SUPPORTED) {
				psa_destroy_key(key);
				ztest_test_skip();
			}
			zassert_equal(PSA_SUCCESS, status);
			zassert_equal(PSA_SUCCESS,
				      psa_aead_set_nonce(&aead_op, v->nonce, CHACHAPOLY_NONCE_LEN));
			aead_update_ad_chunked(v->aad, v->aad_len, chunk);
			out_pos = aead_update_chunked(v->msg, v->msg_len, ram_output, chunk);
			zassert_equal(PSA_SUCCESS, psa_aead_finish(&aead_op, ram_output + out_pos,
								   v->msg_len - out_pos, &out_len,
								   tag, sizeof(tag), &tag_len));
			out_pos += out_len;
			zassert_equal(v->msg_len, out_pos);
			zassert_equal(CHACHAPOLY_TAG_LEN, tag_len);
			zassert_mem_equal(v->ct_tag, ram_output, v->msg_len, "Vector %zu", i);
			zassert_mem_equal(v->ct_tag + v->msg_len, tag, CHACHAPOLY_TAG_LEN,
					  "Vector %zu", i);
		}

		aead_op = psa_aead_operation_init();
		zassert_equal(PSA_SUCCESS,
			      psa_aead_decrypt_setup(&aead_op, key, PSA_ALG_CHACHA20_POLY1305));
		zassert_equal(PSA_SUCCESS,
			      psa_aead_set_nonce(&aead_op, v->nonce, CHACHAPOLY_NONCE_LEN));
		aead_update_ad_chunked(v->aad, v->aad_len, chunk);
		out_pos = aead_update_chunked(v->ct_tag, v->msg_len, ram_output, chunk);
		status = psa_aead_verify(&aead_op, ram_output + out_pos, v->msg_len - out_pos,
					 &out_len, v->ct_tag + v->msg_len, CHACHAPOLY_TAG_LEN);
		if (v->valid) {
			zassert_equal(PSA_SUCCESS, status, "Vector %zu: status %d", i, status);
			zassert_equal(v->msg_len, out_pos + out_len);
			zassert_mem_equal(v->msg, ram_output, v->msg_len, "Vector %zu", i);
		} else {
			zassert_equal(PSA_ERROR_INVALID_SIGNATURE, status, "Vector %zu: status %d",
				      i, status);
			psa_aead_abort(&aead_op);
		}
		zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
	}
}

ZTEST_SUITE(psa_chachapoly, NULL, psa_setup, NULL, NULL, NULL);

ZTEST(psa_hash, test_compute)
{
	uint8_t md[PSA_HASH_MAX_SIZE];
	psa_status_t status;
	const uint8_t *msg;
	size_t md_len;

	zassert_not_equal(0, hash_suites_num, "No hash algorithms enabled");

	for (size_t s = 0; s < hash_suites_num; s++) {
		const struct hash_suite *suite = &hash_suites[s];
		size_t expected_len = PSA_HASH_LENGTH(suite->alg);

		for (size_t i = 0; i < suite->num_vectors; i++) {
			const struct hash_vector *v = &suite->vectors[i];

			for (enum source src = 0; src < SRC_NUM; src++) {
				msg = place(v->msg, v->msg_len, ram_input, sizeof(ram_input), src,
					    i);
				memset(md, 0xAA, sizeof(md));

				status = psa_hash_compute(suite->alg, msg, v->msg_len, md,
							  expected_len, &md_len);
				zassert_equal(PSA_SUCCESS, status, "%s %zu (%s): status %d",
					      suite->name, i, source_names[src], status);
				zassert_equal(expected_len, md_len);
				zassert_mem_equal(v->md, md, md_len, "%s %zu (%s)", suite->name, i,
						  source_names[src]);

				status = psa_hash_compare(suite->alg, msg, v->msg_len, v->md,
							  expected_len);
				zassert_equal(PSA_SUCCESS, status, "%s %zu (%s): status %d",
					      suite->name, i, source_names[src], status);
			}
		}

		/* Digest mismatch */
		memcpy(md, suite->vectors[0].md, expected_len);
		md[expected_len - 1] ^= 0x01;
		status = psa_hash_compare(suite->alg, suite->vectors[0].msg,
					  suite->vectors[0].msg_len, md, expected_len);
		zassert_equal(PSA_ERROR_INVALID_SIGNATURE, status, "%s", suite->name);

		/* Output buffer too small */
		status = psa_hash_compute(suite->alg, suite->vectors[0].msg,
					  suite->vectors[0].msg_len, md, expected_len - 1, &md_len);
		zassert_equal(PSA_ERROR_BUFFER_TOO_SMALL, status, "%s", suite->name);
	}
}

static void hash_update_chunked(psa_hash_operation_t *op, const uint8_t *msg, size_t len,
				size_t chunk)
{
	for (size_t pos = 0; pos < len; pos += chunk) {
		zassert_equal(PSA_SUCCESS, psa_hash_update(op, msg + pos, MIN(chunk, len - pos)));
	}
}

ZTEST(psa_hash, test_multipart)
{
	uint8_t md[PSA_HASH_MAX_SIZE];
	const uint8_t *msg;
	size_t md_len;

	for (size_t s = 0; s < hash_suites_num; s++) {
		const struct hash_suite *suite = &hash_suites[s];
		size_t expected_len = PSA_HASH_LENGTH(suite->alg);

		for (size_t i = 0; i < suite->num_vectors; i++) {
			const struct hash_vector *v = &suite->vectors[i];
			size_t chunk = chunk_sizes[i % ARRAY_SIZE(chunk_sizes)];
			enum source src = i % SRC_NUM;
			size_t split = v->msg_len / 2;

			msg = place(v->msg, v->msg_len, ram_input, sizeof(ram_input), src, i);

			/* First half of the message */
			hash_op = psa_hash_operation_init();
			hash_clone = psa_hash_operation_init();
			zassert_equal(PSA_SUCCESS, psa_hash_setup(&hash_op, suite->alg));
			hash_update_chunked(&hash_op, msg, split, chunk);

			/* Duplicate the intermediate state */
			zassert_equal(PSA_SUCCESS, psa_hash_clone(&hash_op, &hash_clone));

			/* Second half of the message on both operations */
			hash_update_chunked(&hash_op, msg + split, v->msg_len - split, chunk);
			hash_update_chunked(&hash_clone, msg + split, v->msg_len - split, chunk);

			memset(md, 0xAA, sizeof(md));
			zassert_equal(PSA_SUCCESS,
				      psa_hash_finish(&hash_op, md, expected_len, &md_len));
			zassert_equal(expected_len, md_len);
			zassert_mem_equal(v->md, md, md_len, "%s %zu (%s, chunk %zu)", suite->name,
					  i, source_names[src], chunk);
			zassert_equal(PSA_SUCCESS, psa_hash_verify(&hash_clone, v->md, md_len),
				      "%s %zu (%s, chunk %zu)", suite->name, i, source_names[src],
				      chunk);
		}
	}
}

/* NIST SHAVS and SHA3VS Monte Carlo tests */
ZTEST(psa_hash, test_monte_carlo)
{
	psa_status_t status;
	size_t md_len;

	for (size_t s = 0; s < hash_suites_num; s++) {
		const struct hash_suite *suite = &hash_suites[s];
		size_t len = PSA_HASH_LENGTH(suite->alg);
		/* MD[i-3] || MD[i-2] || MD[i-1] for SHA-2, MD[i-1] for SHA-3 */
		uint8_t *md = ram_input;

		zassert_equal(len, suite->mct_seed_len);
		memcpy(md, suite->mct_seed, len);
		if (suite->mct_type == MCT_SHA2) {
			memcpy(md + len, suite->mct_seed, len);
			memcpy(md + 2 * len, suite->mct_seed, len);
		}

		for (size_t j = 0; j < suite->mct_num_checkpoints; j++) {
			const uint8_t *checkpoint = suite->mct_checkpoints + (j * len);

			for (int i = 0; i < 1000; i++) {
				if (suite->mct_type == MCT_SHA2) {
					status = psa_hash_compute(suite->alg, md, 3 * len,
								  ram_output, len, &md_len);
					memmove(md, md + len, 2 * len);
					memcpy(md + 2 * len, ram_output, len);
				} else {
					status = psa_hash_compute(suite->alg, md, len, ram_output,
								  len, &md_len);
					memcpy(md, ram_output, len);
				}
				zassert_equal(PSA_SUCCESS, status);
			}

			if (suite->mct_type == MCT_SHA2) {
				zassert_mem_equal(checkpoint, md + 2 * len, len,
						  "%s checkpoint %zu", suite->name, j);
				/* MD[0] = MD[1] = MD[2] = checkpoint */
				memcpy(md, checkpoint, len);
				memcpy(md + len, checkpoint, len);
			} else {
				zassert_mem_equal(checkpoint, md, len, "%s checkpoint %zu",
						  suite->name, j);
			}
		}
	}
}

ZTEST_SUITE(psa_hash, NULL, psa_setup, NULL, NULL, NULL);

#if defined(CONFIG_PSA_WANT_ALG_HMAC) || defined(CONFIG_PSA_WANT_ALG_HKDF)

/* With statically allocated key slots, larger keys cannot be imported */
#ifdef MBEDTLS_PSA_STATIC_KEY_SLOTS
#define MAX_KEY_LEN MBEDTLS_PSA_STATIC_KEY_SLOT_BUFFER_SIZE
#else
#define MAX_KEY_LEN SIZE_MAX
#endif /* MBEDTLS_PSA_STATIC_KEY_SLOTS */

/* Import a key, returning PSA_KEY_ID_NULL if it is larger than MAX_KEY_LEN */
static psa_key_id_t key_import(psa_key_type_t type, psa_key_usage_t usage, psa_algorithm_t alg,
			       const uint8_t *data, size_t len)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id;
	psa_status_t status;

	psa_set_key_usage_flags(&attr, usage);
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&attr, alg);
	psa_set_key_type(&attr, type);

	status = psa_import_key(&attr, data, len, &key_id);
	if (len > MAX_KEY_LEN) {
		zassert_equal(PSA_ERROR_NOT_SUPPORTED, status, "Key import (%zu bytes): status %d",
			      len, status);
		return PSA_KEY_ID_NULL;
	}
	zassert_equal(PSA_SUCCESS, status, "Key import (%zu bytes): status %d", len, status);
	return key_id;
}

#endif /* CONFIG_PSA_WANT_ALG_HMAC || CONFIG_PSA_WANT_ALG_HKDF */

#ifdef CONFIG_PSA_WANT_ALG_HMAC

static psa_mac_operation_t mac_op;

static psa_key_id_t hmac_key_import(psa_algorithm_t alg, const uint8_t *key, size_t key_len)
{
	return key_import(PSA_KEY_TYPE_HMAC,
			  PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE, alg, key,
			  key_len);
}

ZTEST(psa_hmac, test_compute)
{
	uint8_t mac[PSA_MAC_MAX_SIZE];
	psa_status_t status;
	const uint8_t *data;
	psa_key_id_t key;
	size_t mac_len;

	zassert_not_equal(0, mac_suites_num, "No HMAC algorithms enabled");

	for (size_t s = 0; s < mac_suites_num; s++) {
		const struct mac_suite *suite = &mac_suites[s];
		size_t expected_len = PSA_HASH_LENGTH(PSA_ALG_HMAC_GET_HASH(suite->alg));

		for (size_t i = 0; i < mac_inputs_num; i++) {
			const struct mac_input *v = &mac_inputs[i];
			const uint8_t *expected = suite->macs + (i * expected_len);

			key = hmac_key_import(suite->alg, v->key, v->key_len);
			if (key == PSA_KEY_ID_NULL) {
				/* Key larger than the key slots */
				continue;
			}

			for (enum source src = 0; src < SRC_NUM; src++) {
				data = place(v->data, v->data_len, ram_input, sizeof(ram_input),
					     src, i);
				memset(mac, 0xAA, sizeof(mac));

				status = psa_mac_compute(key, suite->alg, data, v->data_len, mac,
							 expected_len, &mac_len);
				zassert_equal(PSA_SUCCESS, status, "%s %zu (%s): status %d",
					      suite->name, i, source_names[src], status);
				zassert_equal(expected_len, mac_len);
				zassert_mem_equal(expected, mac, mac_len, "%s %zu (%s)",
						  suite->name, i, source_names[src]);

				status = psa_mac_verify(key, suite->alg, data, v->data_len,
							expected, expected_len);
				zassert_equal(PSA_SUCCESS, status, "%s %zu (%s): status %d",
					      suite->name, i, source_names[src], status);
			}

			/* MAC mismatch */
			memcpy(mac, expected, expected_len);
			mac[expected_len - 1] ^= 0x01;
			status = psa_mac_verify(key, suite->alg, v->data, v->data_len, mac,
						expected_len);
			zassert_equal(PSA_ERROR_INVALID_SIGNATURE, status, "%s %zu", suite->name,
				      i);

			/* Output buffer too small */
			status = psa_mac_compute(key, suite->alg, v->data, v->data_len, mac,
						 expected_len - 1, &mac_len);
			zassert_equal(PSA_ERROR_BUFFER_TOO_SMALL, status, "%s %zu", suite->name, i);

			zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
		}
	}
}

static void mac_update_chunked(const uint8_t *data, size_t len, size_t chunk)
{
	for (size_t pos = 0; pos < len; pos += chunk) {
		zassert_equal(PSA_SUCCESS,
			      psa_mac_update(&mac_op, data + pos, MIN(chunk, len - pos)));
	}
}

ZTEST(psa_hmac, test_multipart)
{
	uint8_t mac[PSA_MAC_MAX_SIZE];
	const uint8_t *data;
	psa_key_id_t key;
	size_t mac_len;

	for (size_t s = 0; s < mac_suites_num; s++) {
		const struct mac_suite *suite = &mac_suites[s];
		size_t expected_len = PSA_HASH_LENGTH(PSA_ALG_HMAC_GET_HASH(suite->alg));

		for (size_t i = 0; i < mac_inputs_num; i++) {
			const struct mac_input *v = &mac_inputs[i];
			const uint8_t *expected = suite->macs + (i * expected_len);
			size_t chunk = chunk_sizes[(s + i) % ARRAY_SIZE(chunk_sizes)];
			enum source src = (s + i) % SRC_NUM;

			key = hmac_key_import(suite->alg, v->key, v->key_len);
			if (key == PSA_KEY_ID_NULL) {
				/* Key larger than the key slots */
				continue;
			}
			data = place(v->data, v->data_len, ram_input, sizeof(ram_input), src, i);

			mac_op = psa_mac_operation_init();
			zassert_equal(PSA_SUCCESS, psa_mac_sign_setup(&mac_op, key, suite->alg));
			mac_update_chunked(data, v->data_len, chunk);
			memset(mac, 0xAA, sizeof(mac));
			zassert_equal(PSA_SUCCESS,
				      psa_mac_sign_finish(&mac_op, mac, expected_len, &mac_len));
			zassert_equal(expected_len, mac_len);
			zassert_mem_equal(expected, mac, mac_len, "%s %zu (%s, chunk %zu)",
					  suite->name, i, source_names[src], chunk);

			mac_op = psa_mac_operation_init();
			zassert_equal(PSA_SUCCESS, psa_mac_verify_setup(&mac_op, key, suite->alg));
			mac_update_chunked(data, v->data_len, chunk);
			zassert_equal(
				PSA_SUCCESS, psa_mac_verify_finish(&mac_op, expected, expected_len),
				"%s %zu (%s, chunk %zu)", suite->name, i, source_names[src], chunk);

			zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
		}
	}
}

ZTEST_SUITE(psa_hmac, NULL, psa_setup, NULL, NULL, NULL);

#endif /* CONFIG_PSA_WANT_ALG_HMAC */

#ifdef CONFIG_PSA_WANT_ALG_HKDF

#define MAX_OKM_LEN 512

static psa_key_derivation_operation_t kdf_op;
static uint8_t okm[MAX_OKM_LEN];

ZTEST(psa_hkdf, test_derive)
{
	const uint8_t *salt, *info, *ikm, *expected;
	psa_status_t status;
	psa_key_id_t key;
	size_t split;

	zassert_not_equal(0, kdf_suites_num, "No HKDF algorithms enabled");

	for (size_t s = 0; s < kdf_suites_num; s++) {
		const struct kdf_suite *suite = &kdf_suites[s];

		expected = suite->okms;
		for (size_t i = 0; i < kdf_inputs_num; i++) {
			const struct kdf_input *v = &kdf_inputs[i];

			zassert_true(v->okm_len <= sizeof(okm));
			/* PSA_KEY_ID_NULL if the IKM is larger than the key slots */
			key = key_import(PSA_KEY_TYPE_DERIVE, PSA_KEY_USAGE_DERIVE, suite->alg,
					 v->ikm, v->ikm_len);

			for (enum source src = 0; src < SRC_NUM; src++) {
				salt = place(v->salt, v->salt_len, ram_aad, sizeof(ram_aad), src,
					     i);
				info = place(v->info, v->info_len, ram_input, sizeof(ram_input),
					     src, i);
				memset(okm, 0xAA, sizeof(okm));

				kdf_op = psa_key_derivation_operation_init();
				zassert_equal(PSA_SUCCESS,
					      psa_key_derivation_setup(&kdf_op, suite->alg));
				zassert_equal(PSA_SUCCESS,
					      psa_key_derivation_input_bytes(
						      &kdf_op, PSA_KEY_DERIVATION_INPUT_SALT, salt,
						      v->salt_len));
				if ((src == SRC_RAM) && (key != PSA_KEY_ID_NULL)) {
					status = psa_key_derivation_input_key(
						&kdf_op, PSA_KEY_DERIVATION_INPUT_SECRET, key);
				} else {
					/* Secret provided directly, no key slot required */
					ikm = place(v->ikm, v->ikm_len, ram_output,
						    sizeof(ram_output), src, i);
					status = psa_key_derivation_input_bytes(
						&kdf_op, PSA_KEY_DERIVATION_INPUT_SECRET, ikm,
						v->ikm_len);
				}
				zassert_equal(PSA_SUCCESS, status, "%s %zu (%s): status %d",
					      suite->name, i, source_names[src], status);
				zassert_equal(PSA_SUCCESS,
					      psa_key_derivation_input_bytes(
						      &kdf_op, PSA_KEY_DERIVATION_INPUT_INFO, info,
						      v->info_len));

				/* Output in two parts, the split varying across the HMAC blocks */
				split = (src * v->okm_len) / SRC_NUM;
				zassert_equal(PSA_SUCCESS,
					      psa_key_derivation_output_bytes(&kdf_op, okm, split));
				zassert_equal(PSA_SUCCESS,
					      psa_key_derivation_output_bytes(&kdf_op, okm + split,
									      v->okm_len - split));
				zassert_mem_equal(expected, okm, v->okm_len, "%s %zu (%s)",
						  suite->name, i, source_names[src]);
				zassert_equal(PSA_SUCCESS, psa_key_derivation_abort(&kdf_op));
			}

			if (key != PSA_KEY_ID_NULL) {
				zassert_equal(PSA_SUCCESS, psa_destroy_key(key));
			}
			expected += v->okm_len;
		}
	}
}

ZTEST_SUITE(psa_hkdf, NULL, psa_setup, NULL, NULL, NULL);

#endif /* CONFIG_PSA_WANT_ALG_HKDF */
