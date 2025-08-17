/*
 * Copyright (c) 2024 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <math.h>

#include <zephyr/ztest.h>

#include <infuse/math/hamming.h>

uint8_t input[256];
uint8_t encoded[512];
uint8_t output[256];

ZTEST(infuse_hamming, test_bad_buffers)
{
	int rc;

	for (int i = 0; i < 20; i++) {
		rc = hamming_8_4_encode(input, 10, encoded, i);
		zassert_equal(-EINVAL, rc);
		rc = hamming_8_4_decode(encoded, 40, output, i);
		zassert_equal(-EINVAL, rc);
	}
}

ZTEST(infuse_hamming, test_no_errors)
{
	int rc;

	rc = hamming_8_4_encode(input, sizeof(input), encoded, sizeof(encoded));
	zassert_equal(sizeof(encoded), rc);

	rc = hamming_8_4_decode(encoded, sizeof(encoded), output, sizeof(output));
	zassert_equal(sizeof(input), rc);

	for (int i = 0; i < sizeof(input); i++) {
		zassert_equal(input[i], output[i]);
	}
}

ZTEST(infuse_hamming, test_input_lengths)
{
	int rc;

	for (int i = 0; i < sizeof(input); i++) {
		rc = hamming_8_4_encode(input, i, encoded, sizeof(encoded));
		zassert_equal(2 * i, rc);

		rc = hamming_8_4_decode(encoded, 2 * i, output, sizeof(output));
		zassert_equal(i, rc);

		for (int j = 0; j < i; j++) {
			zassert_equal(input[j], output[j]);
		}
	}
}

ZTEST(infuse_hamming, test_decode_lengths)
{
	int rc;

	rc = hamming_8_4_encode(input, sizeof(input), encoded, sizeof(encoded));
	zassert_equal(sizeof(encoded), rc);

	for (int i = 0; i < sizeof(encoded); i++) {
		rc = hamming_8_4_decode(encoded, i, output, sizeof(output));
		zassert_equal(i / 2, rc);
	}
}

ZTEST(infuse_hamming, test_single_errors)
{
	int rc;

	for (int i = 0; i < 8; i++) {
		rc = hamming_8_4_encode(input, sizeof(input), encoded, sizeof(encoded));
		zassert_equal(sizeof(encoded), rc);

		/* Corrupt bit i of codewords */
		for (int j = 0; j < sizeof(encoded); j++) {
			encoded[j] ^= BIT(i);
		}

		/* All data should be successfully decoded */
		rc = hamming_8_4_decode(encoded, sizeof(encoded), output, sizeof(output));
		zassert_equal(sizeof(input), rc);
		for (int j = 0; j < sizeof(input); j++) {
			zassert_equal(input[j], output[j]);
		}
	}
}

ZTEST(infuse_hamming, test_double_errors)
{
	int rc;

	for (int i = 0; i < sizeof(encoded); i++) {
		rc = hamming_8_4_encode(input, sizeof(input), encoded, sizeof(encoded));
		zassert_equal(sizeof(encoded), rc);

		/* Double corrupt byte i of payload */
		encoded[i] ^= (0x14 << (i % 3));

		/* All data up to the double error should be decoded correctly */
		rc = hamming_8_4_decode(encoded, sizeof(encoded), output, sizeof(output));
		zassert_equal(i, rc);
		for (int j = 0; j < i / 2; j++) {
			zassert_equal(input[j], output[j]);
		}
	}
}

void *test_init(void)
{
	for (int i = 0; i < sizeof(input); i++) {
		input[i] = i;
	}
	return NULL;
}

ZTEST_SUITE(infuse_hamming, NULL, test_init, NULL, NULL, NULL);
