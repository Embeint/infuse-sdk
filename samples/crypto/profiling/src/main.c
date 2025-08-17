/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/timing/timing.h>

#include <psa/crypto.h>
#include <psa/crypto_extra.h>

#include <mbedtls/poly1305.h>

#include <infuse/crypto/ascon.h>
#include <infuse/crypto/xoodyak.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define REPEATS 4

static const uint16_t plaintext_lengths[] = {1, 16, 64, 128, 256, 512, 1024};

uint8_t plaintext[1024];
uint8_t ciphertext[1024 + 16];
uint8_t decrypted[1024];
uint8_t hash[32];
uint8_t signature[64];

enum algorithms {
	ASCON_128A = 0,
	ASCON_128,
	ASCON_80PQ,
	XOODYAK,
	CHACHA20_POLY1305,
	NUM_ALGORITHMS,
};

const char *const algorithm_names[] = {
	[ASCON_128] = "ascon-128",
	[ASCON_128A] = "ascon-128a",
	[ASCON_80PQ] = "ascon-80pq",
	[XOODYAK] = "xoodyak",
	[CHACHA20_POLY1305] = "chacha20-poly1305",
};

enum sign_alg {
	SHA256 = 0,
	HMAC_SHA256,
	CMAC,
	ECDSA_SHA256,
	POLY1305,
	NUM_SIGN_ALGORITHMS,
};

const char *const sign_algorithm_names[] = {
	[SHA256] = "SHA256",
	[HMAC_SHA256] = "HMAC-SHA256",
	[ECDSA_SHA256] = "ECDSA-SHA256",
	[POLY1305] = "POLY1305",
	[CMAC] = "CMAC",
};

uint64_t encrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};
uint64_t decrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};
uint64_t sign_cycles[NUM_SIGN_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};

psa_key_id_t chacha_key_id, hmac_key_id, cmac_key_id, ecdsa_key_id, poly1305_key_id;

static int key_setup(uint8_t key[32])
{
	psa_key_attributes_t key_attributes;
	psa_status_t status;

	/* Crypto settings for Chacha20-Poly1305 */
	key_attributes = psa_key_attributes_init();
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_CHACHA20_POLY1305);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&key_attributes, 256);

	/* Import Chacha20 key */
	status = psa_import_key(&key_attributes, key, 32, &chacha_key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Import key failed! (%d)", status);
		return -1;
	}

	/* Crypto settings for HMAC */
	key_attributes = psa_key_attributes_init();
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&key_attributes, 256);

	/* Import HMAC key */
	status = psa_generate_key(&key_attributes, &hmac_key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to import HMAC key! (%d)", status);
		return -1;
	}

	/* Crypto settings for CMAC */
	key_attributes = psa_key_attributes_init();
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_CMAC);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&key_attributes, 256);

	/* Import CMAC key */
	status = psa_generate_key(&key_attributes, &cmac_key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to import CMAC key! (%d)", status);
		return -1;
	}

	/* Crypto settings for ECDSA */
	key_attributes = psa_key_attributes_init();
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_SIGN_HASH);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&key_attributes, 256);

#ifdef CONFIG_PSA_WANT_ALG_ECDSA
	/* Import ECDSA key */
	status = psa_generate_key(&key_attributes, &ecdsa_key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to generate key pair! (%d)", status);
		return -1;
	}
#endif /* CONFIG_PSA_WANT_ALG_ECDSA */

	return 0;
}

int main(void)
{
	psa_status_t status;
	uint8_t associated_data[16];
	uint8_t nonce[16];
	uint8_t key[32];
	uint8_t tag[16];
	int rc = 0;

	timing_t start_time, end_time;

	/* Randomise inputs */
	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(associated_data, sizeof(associated_data));
	sys_rand_get(nonce, sizeof(nonce));
	sys_rand_get(key, sizeof(key));

	/* Create PSA key identities */
	if (key_setup(key) < 0) {
		k_sleep(K_FOREVER);
	}

	/* Start hardware cycle counters */
	timing_init();
	timing_start();

	for (int i = 0; i < ARRAY_SIZE(plaintext_lengths); i++) {
#ifdef CONFIG_CRYPTO_ASCON_128A
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon128a_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i],
					       associated_data, 4, tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_128A][i][r] =
				timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
						    associated_data, 4, nonce, key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_128A][i][r] =
				timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_128A */
#ifdef CONFIG_CRYPTO_ASCON_128
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon128_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i],
					      associated_data, 4, tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_128][i][r] = timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
						   associated_data, 4, nonce, key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_128][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_128 */
#ifdef CONFIG_CRYPTO_ASCON_80PQ
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon80pq_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i],
					       associated_data, 4, tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_80PQ][i][r] =
				timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
						    associated_data, 4, nonce, key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_80PQ][i][r] =
				timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_80PQ */
#ifdef CONFIG_CRYPTO_XOODYAK
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			xoodyak_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i],
					     associated_data, 4, tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[XOODYAK][i][r] = timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = xoodyak_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen,
						  associated_data, 4, nonce, key);
			end_time = timing_counter_get();

			decrypt_cycles[XOODYAK][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_XOODYAK */
		for (int r = 0; r < REPEATS; r++) {
			size_t clen, mlen;

			start_time = timing_counter_get();

			status = psa_aead_encrypt(chacha_key_id, PSA_ALG_CHACHA20_POLY1305, nonce,
						  12, associated_data, 4, plaintext,
						  plaintext_lengths[i], ciphertext,
						  sizeof(ciphertext), &clen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_aead_encrypt failed! (Error: %d)", status);
			}
			encrypt_cycles[CHACHA20_POLY1305][i][r] =
				timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			status = psa_aead_decrypt(chacha_key_id, PSA_ALG_CHACHA20_POLY1305, nonce,
						  12, associated_data, 4, ciphertext, clen,
						  decrypted, sizeof(decrypted), &mlen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_aead_decrypt failed! (Error: %d)", status);
			}
			decrypt_cycles[CHACHA20_POLY1305][i][r] =
				timing_cycles_get(&start_time, &end_time);
		}
		for (int r = 0; r < REPEATS; r++) {
			size_t hlen;

			start_time = timing_counter_get();
			status = psa_hash_compute(PSA_ALG_SHA_256, plaintext, plaintext_lengths[i],
						  hash, sizeof(hash), &hlen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_sign_hash failed! (Error: %d)", status);
			}
			sign_cycles[SHA256][i][r] = timing_cycles_get(&start_time, &end_time);
		}
		for (int r = 0; r < REPEATS; r++) {
			mbedtls_poly1305_context ctx;

			start_time = timing_counter_get();
			mbedtls_poly1305_init(&ctx);
			mbedtls_poly1305_starts(&ctx, key);
			mbedtls_poly1305_update(&ctx, plaintext, plaintext_lengths[i]);
			mbedtls_poly1305_finish(&ctx, signature);
			mbedtls_poly1305_free(&ctx);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_sign_hash failed! (Error: %d)", status);
			}
			sign_cycles[POLY1305][i][r] = timing_cycles_get(&start_time, &end_time);
		}
		for (int r = 0; r < REPEATS; r++) {
			size_t slen;

			start_time = timing_counter_get();
			status = psa_mac_compute(hmac_key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
						 plaintext, plaintext_lengths[i], signature,
						 sizeof(signature), &slen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_mac_compute failed! (Error: %d)", status);
			}
			sign_cycles[HMAC_SHA256][i][r] = timing_cycles_get(&start_time, &end_time);
		}
		for (int r = 0; r < REPEATS; r++) {
			size_t slen;

			start_time = timing_counter_get();
			status = psa_mac_compute(cmac_key_id, PSA_ALG_CMAC, plaintext,
						 plaintext_lengths[i], signature, sizeof(signature),
						 &slen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_mac_compute failed! (Error: %d)", status);
			}
			sign_cycles[CMAC][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#ifdef CONFIG_PSA_WANT_ALG_ECDSA
		/* ECDSA takes a very long time, only run once */
		for (int r = 0; r < 1; r++) {
			size_t hlen, slen;

			start_time = timing_counter_get();
			status = psa_hash_compute(PSA_ALG_SHA_256, plaintext, plaintext_lengths[i],
						  hash, sizeof(hash), &hlen);
			if (status == PSA_SUCCESS) {
				status = psa_sign_hash(ecdsa_key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
						       hash, sizeof(hash), signature,
						       sizeof(signature), &slen);
			}
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_sign_hash failed! (Error: %d)", status);
			}
			sign_cycles[ECDSA_SHA256][i][r] = timing_cycles_get(&start_time, &end_time);
		}
		for (int r = 1; r < REPEATS; r++) {
			sign_cycles[ECDSA_SHA256][i][r] = sign_cycles[ECDSA_SHA256][i][0];
		}
#endif /* CONFIG_PSA_WANT_ALG_ECDSA */
	}

	/* Log timing results */
	LOG_INF("ASCON backend - %s", CONFIG_CRYPTO_ASCON_IMPL);
	for (int i = 0; i < NUM_ALGORITHMS; i++) {
		LOG_INF("%s", algorithm_names[i]);
		LOG_INF("\t%6s | %17s | %17s", "Length", "Enc: Cycles (ns)", "Dec: Cycles (ns)");
		for (int j = 0; j < ARRAY_SIZE(plaintext_lengths); j++) {
			/* Average results */
			uint64_t encr_avg = 0;
			uint64_t decr_avg = 0;
			uint64_t encr_ns, decr_ns;

			for (int k = 0; k < REPEATS; k++) {
				encr_avg += encrypt_cycles[i][j][k];
				decr_avg += decrypt_cycles[i][j][k];
			}
			encr_avg /= REPEATS;
			decr_avg /= REPEATS;

			encr_ns = timing_cycles_to_ns(encr_avg);
			decr_ns = timing_cycles_to_ns(decr_avg);

			LOG_INF("\t%6d |  %6llu (%7llu) |  %6llu (%7llu)", plaintext_lengths[j],
				encr_avg, encr_ns, decr_avg, decr_ns);
		}
	}

	LOG_INF("");
	LOG_INF("MAC/HASH Algorithms");
	for (int i = 0; i < NUM_SIGN_ALGORITHMS; i++) {
		LOG_INF("%s", sign_algorithm_names[i]);
		LOG_INF("\t%6s | %17s", "Length", "Sign: Cycles (ns)");
		for (int j = 0; j < ARRAY_SIZE(plaintext_lengths); j++) {
			/* Average results */
			uint64_t sign_avg = 0;
			uint64_t sign_ns;

			for (int k = 0; k < REPEATS; k++) {
				sign_avg += sign_cycles[i][j][k];
			}
			sign_avg /= REPEATS;

			sign_ns = timing_cycles_to_ns(sign_avg);
			LOG_INF("\t%6d |  %6llu (%7llu)", plaintext_lengths[j], sign_avg, sign_ns);
		}
	}
	LOG_INF("Sample complete");
	k_sleep(K_FOREVER);
	return 0;
}
