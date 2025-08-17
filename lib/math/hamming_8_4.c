/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>

#include <stdio.h>

#include <zephyr/toolchain.h>

#include <infuse/math/hamming.h>

static uint8_t encode_table[16] = {
	[0b0000] = 0b00010101, [0b0001] = 0b00000010, [0b0010] = 0b01001001, [0b0011] = 0b01011110,
	[0b0100] = 0b01100100, [0b0101] = 0b01110011, [0b0110] = 0b00111000, [0b0111] = 0b00101111,
	[0b1000] = 0b11010000, [0b1001] = 0b11000111, [0b1010] = 0b10001100, [0b1011] = 0b10011011,
	[0b1100] = 0b10100001, [0b1101] = 0b10110110, [0b1110] = 0b11111101, [0b1111] = 0b11101010,
};

static uint8_t compute_syndrome(uint8_t codeword)
{
	uint8_t b7 = (codeword >> 7) & 1;
	uint8_t b6 = (codeword >> 6) & 1;
	uint8_t b5 = (codeword >> 5) & 1;
	uint8_t b4 = (codeword >> 4) & 1;
	uint8_t b3 = (codeword >> 3) & 1;
	uint8_t b2 = (codeword >> 2) & 1;
	uint8_t b1 = (codeword >> 1) & 1;
	uint8_t b0 = (codeword >> 0) & 1;

	uint8_t s0 = b7 ^ b5 ^ b1 ^ b0;
	uint8_t s1 = b7 ^ b3 ^ b2 ^ b1;
	uint8_t s2 = b5 ^ b4 ^ b3 ^ b1;
	uint8_t s3 = b7 ^ b6 ^ b5 ^ b4 ^ b3 ^ b2 ^ b1 ^ b0;

	return (s0 << 3) | (s1 << 2) | (s2 << 1) | s3;
}

static uint8_t correct_error(uint8_t data, uint8_t syndrome)
{
	switch (syndrome) {
	case 0b0000:
		return data ^= 0b0001;
	case 0b1000:
		return data ^= 0b0010;
	case 0b0100:
		return data ^= 0b0100;
	case 0b0010:
		return data ^= 0b1000;
	}
	/* Error was in one of the parity bits */
	return data;
}

static uint8_t extract_data(uint8_t codeword)
{
	uint8_t b7 = (codeword >> 7) & 1;
	uint8_t b5 = (codeword >> 5) & 1;
	uint8_t b3 = (codeword >> 3) & 1;
	uint8_t b1 = (codeword >> 1) & 1;

	return (b7 << 3) | (b5 << 2) | (b3 << 1) | (b1 << 0);
}

uint8_t decode_codeword(uint8_t codeword, int *error_detected, int *double_error)
{
	uint8_t syndrome = compute_syndrome(codeword);
	uint8_t data = extract_data(codeword);

	*error_detected = syndrome != 0xF;
	*double_error = *error_detected & (syndrome & 1);

	if (*error_detected && !*double_error) {
		data = correct_error(data, syndrome);
	}

	return data;
}

int hamming_8_4_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len)
{
	size_t output_offset = 0;

	if (output_len < (2 * input_len)) {
		return -EINVAL;
	}
	/* Use the precomputed table for simplicity */
	for (int i = 0; i < input_len; i++) {
		output[output_offset++] = encode_table[(input[i] & 0xF0) >> 4];
		output[output_offset++] = encode_table[(input[i] & 0x0F) >> 0];
	}
	return output_offset;
}

int hamming_8_4_decode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len)
{
	int error_detected, double_error;
	uint8_t out;

	if ((output_len * 2) < input_len) {
		return -EINVAL;
	}

	for (int i = 0; i < input_len; i += 2) {
		out = decode_codeword(input[i], &error_detected, &double_error) << 4;
		if (double_error) {
			return i;
		}
		out |= decode_codeword(input[i + 1], &error_detected, &double_error) << 0;
		if (double_error) {
			return i + 1;
		}
		output[i / 2] = out;
	}

	return input_len / 2;
}
