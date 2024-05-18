/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/timing/timing.h>

#include <psa/crypto.h>
#include <psa/crypto_extra.h>

#include <infuse/crypto/ascon.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define REPEATS 4

static const uint16_t plaintext_lengths[] = {1, 16, 64, 128, 256, 512, 1024};

uint8_t plaintext[1024];
uint8_t ciphertext[1024 + 16];
uint8_t decrypted[1024];

enum algorithms {
	ASCON_128A = 0,
	ASCON_128,
	ASCON_80PQ,
	CHACHA20_POLY1305,
	NUM_ALGORITHMS,
};

const char *algorithm_names[] = {
	[ASCON_128] = "ascon-128",
	[ASCON_128A] = "ascon-128a",
	[ASCON_80PQ] = "ascon-80pq",
	[CHACHA20_POLY1305] = "chacha20-poly1305",
};

uint64_t encrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};
uint64_t decrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};

int main(void)
{
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

	/* Initialize PSA Crypto */
	psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id;
	psa_status_t status;

	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		LOG_ERR("PSA init failed! (%d)", status);
		k_sleep(K_FOREVER);
	}

	/* Crypto settings for Chacha20-Poly1305 */
	psa_set_key_usage_flags(&key_attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_algorithm(&key_attributes, PSA_ALG_CHACHA20_POLY1305);
	psa_set_key_type(&key_attributes, PSA_KEY_TYPE_CHACHA20);
	psa_set_key_bits(&key_attributes, 256);

	/* Import Chacha20 key */
	status = psa_import_key(&key_attributes, key, 32, &key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Import key failed! (%d)", status);
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
		for (int r = 0; r < REPEATS; r++) {
			size_t clen, mlen;

			start_time = timing_counter_get();

			status = psa_aead_encrypt(key_id, PSA_ALG_CHACHA20_POLY1305, nonce, 12,
						  associated_data, 4, plaintext,
						  plaintext_lengths[i], ciphertext,
						  sizeof(ciphertext), &clen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_aead_encrypt failed! (Error: %d)", status);
			}
			encrypt_cycles[CHACHA20_POLY1305][i][r] =
				timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			status = psa_aead_decrypt(key_id, PSA_ALG_CHACHA20_POLY1305, nonce, 12,
						  associated_data, 4, ciphertext, clen, decrypted,
						  sizeof(decrypted), &mlen);
			end_time = timing_counter_get();
			if (status != PSA_SUCCESS) {
				LOG_INF("psa_aead_decrypt failed! (Error: %d)", status);
			}
			decrypt_cycles[CHACHA20_POLY1305][i][r] =
				timing_cycles_get(&start_time, &end_time);
		}
	}

	/* Log timing results */
	LOG_INF("ASCON backend - %s", CONFIG_CRYPTO_ASCON_IMPL);
	for (int i = 0; i < NUM_ALGORITHMS; i++) {
		LOG_INF("%s", algorithm_names[i]);
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

			LOG_INF("\tLength %4d Encrypt %6llu (%7llu ns) Decrypt %6llu (%7llu ns)",
				plaintext_lengths[j], encr_avg, encr_ns, decr_avg, decr_ns);
		}
	}
	k_sleep(K_FOREVER);
	return 0;
}
