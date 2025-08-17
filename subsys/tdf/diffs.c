/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/sys/byteorder.h>

#include "diffs.h"

bool tdf_diff_check_16_8(int tdf_len, const void *current, const void *next)
{
	const uint16_t *c = current, *n = next;
	uint16_t c_val, n_val, res;
	int8_t diff;

	for (int i = 0; i < (tdf_len / sizeof(uint16_t)); i++) {
		c_val = UNALIGNED_GET(c);
		n_val = UNALIGNED_GET(n);
		/* Calculate the 8 bit diff */
		diff = n_val - c_val;
		res = c_val + diff;
		/* Validate it gets us to the next value*/
		if (res != n_val) {
			return false;
		}
		n++;
		c++;
	}
	return true;
}

void tdf_diff_encode_16_8(int num_fields, const void *current, const void *next, void *out)
{
	const uint16_t *c = current, *n = next;
	int8_t *diff = out;

	for (int i = 0; i < num_fields; i++) {
		diff[i] = UNALIGNED_GET(n) - UNALIGNED_GET(c);
		c++;
		n++;
	}
}

void tdf_diff_apply_16_8(uint8_t tdf_len, const void *base, void *out, void *diffs)
{
	const uint16_t *b = base;
	uint16_t *o = out;
	int8_t *d = diffs;

	for (int i = 0; i < (tdf_len / sizeof(uint16_t)); i++) {
		o[i] = b[i] + d[i];
	}
}

bool tdf_diff_check_32_8(int tdf_len, const void *current, const void *next)
{
	const uint32_t *c = current, *n = next;
	uint32_t c_val, n_val, res;
	int8_t diff;

	for (int i = 0; i < (tdf_len / sizeof(uint32_t)); i++) {
		c_val = UNALIGNED_GET(c);
		n_val = UNALIGNED_GET(n);
		/* Calculate the 8 bit diff */
		diff = n_val - c_val;
		res = c_val + diff;
		/* Validate it gets us to the next value*/
		if (res != n_val) {
			return false;
		}
		n++;
		c++;
	}
	return true;
}

void tdf_diff_encode_32_8(int num_fields, const void *current, const void *next, void *out)
{
	const uint32_t *c = current, *n = next;
	int8_t *diff = out;

	for (int i = 0; i < num_fields; i++) {
		diff[i] = UNALIGNED_GET(n) - UNALIGNED_GET(c);
		c++;
		n++;
	}
}

void tdf_diff_apply_32_8(uint8_t tdf_len, const void *base, void *out, void *diffs)
{
	const uint32_t *b = base;
	uint32_t *o = out;
	int8_t *d = diffs;

	for (int i = 0; i < (tdf_len / sizeof(uint32_t)); i++) {
		o[i] = b[i] + d[i];
	}
}

bool tdf_diff_check_32_16(int tdf_len, const void *current, const void *next)
{
	const uint32_t *c = current, *n = next;
	uint32_t c_val, n_val, res;
	int16_t diff;

	for (int i = 0; i < (tdf_len / sizeof(uint32_t)); i++) {
		c_val = UNALIGNED_GET(c);
		n_val = UNALIGNED_GET(n);
		/* Calculate the 16 bit diff */
		diff = n_val - c_val;
		res = c_val + diff;
		/* Validate it gets us to the next value*/
		if (res != n_val) {
			return false;
		}
		n++;
		c++;
	}
	return true;
}

void tdf_diff_encode_32_16(int num_fields, const void *current, const void *next, void *out)
{
	const uint32_t *c = current, *n = next;
	int16_t *diff = out;

	for (int i = 0; i < num_fields; i++) {
		diff[i] = UNALIGNED_GET(n) - UNALIGNED_GET(c);
		c++;
		n++;
	}
}

void tdf_diff_apply_32_16(uint8_t tdf_len, const void *base, void *out, void *diffs)
{
	const uint32_t *b = base;
	uint32_t *o = out;
	int16_t *d = diffs;

	for (int i = 0; i < (tdf_len / sizeof(uint32_t)); i++) {
		o[i] = b[i] + d[i];
	}
}
