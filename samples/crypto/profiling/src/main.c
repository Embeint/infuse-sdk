/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/timing/timing.h>

#include <eis/crypto/ascon.h>

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
	NUM_ALGORITHMS,
};

const char *algorithm_names[] = {
	[ASCON_128] = "ascon-128",
	[ASCON_128A] = "ascon-128a",
	[ASCON_80PQ] = "ascon-80pq",
};

uint64_t encrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};
uint64_t decrypt_cycles[NUM_ALGORITHMS][ARRAY_SIZE(plaintext_lengths)][REPEATS] = {0};

int main(void)
{
	uint8_t associated_data[16];
	uint8_t nonce[16];
	uint8_t key[16];
	uint8_t tag[16];
	int rc = 0;

	timing_t start_time, end_time;

	/* Randomise inputs */
	sys_rand_get(plaintext, sizeof(plaintext));
	sys_rand_get(associated_data, sizeof(associated_data));
	sys_rand_get(nonce, sizeof(nonce));
	sys_rand_get(key, sizeof(key));

	/* Start hardware cycle counters */
	timing_init();
	timing_start();

	for (int i = 0; i < ARRAY_SIZE(plaintext_lengths); i++) {
#ifdef CONFIG_CRYPTO_ASCON_128A
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon128a_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i], associated_data, 4,
					       tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_128A][i][r] = timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon128a_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4, nonce,
						    key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_128A][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_128A */
#ifdef CONFIG_CRYPTO_ASCON_128
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon128_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i], associated_data, 4,
					      tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_128][i][r] = timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon128_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4, nonce,
						   key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_128][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_128 */
#ifdef CONFIG_CRYPTO_ASCON_80PQ
		for (int r = 0; r < REPEATS; r++) {
			unsigned long long clen;
			unsigned long long mlen;

			start_time = timing_counter_get();
			ascon80pq_aead_encrypt(ciphertext, &clen, plaintext, plaintext_lengths[i], associated_data, 4,
					       tag, nonce, key);
			end_time = timing_counter_get();

			encrypt_cycles[ASCON_80PQ][i][r] = timing_cycles_get(&start_time, &end_time);

			start_time = timing_counter_get();
			rc = ascon80pq_aead_decrypt(decrypted, &mlen, tag, ciphertext, clen, associated_data, 4, nonce,
						    key);
			end_time = timing_counter_get();

			decrypt_cycles[ASCON_80PQ][i][r] = timing_cycles_get(&start_time, &end_time);
		}
#endif /* CONFIG_CRYPTO_ASCON_80PQ */
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

			LOG_INF("\tLength %4d Encrypt %6llu (%7llu ns) Decrypt %6llu (%7llu ns)", plaintext_lengths[j],
				encr_avg, encr_ns, decr_avg, decr_ns);
		}
	}
	k_sleep(K_FOREVER);
	return 0;
}
