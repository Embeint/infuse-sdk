/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/sys/util.h>

#include <infuse/math/common.h>

uint8_t math_sqrt16(uint16_t x)
{
	uint16_t rem = 0, root = 0;

	for (int i = 16 / 2; i > 0; i--) {
		root <<= 1;
		rem = (rem << 2) | (x >> (16 - 2));
		x <<= 2;
		if (root < rem) {
			rem -= root | 1;
			root += 2;
		}
	}
	return root >> 1;
}

uint16_t math_sqrt32(uint32_t x)
{
	uint32_t rem = 0, root = 0;

	for (int i = 32 / 2; i > 0; i--) {
		root <<= 1;
		rem = (rem << 2) | (x >> (32 - 2));
		x <<= 2;
		if (root < rem) {
			rem -= root | 1;
			root += 2;
		}
	}
	return root >> 1;
}

uint32_t math_sqrt64(uint64_t x)
{
	uint32_t rem = 0, root = 0;

	for (int i = 64 / 2; i > 0; i--) {
		root <<= 1;
		rem = (rem << 2) | (x >> (64 - 2));
		x <<= 2;
		if (root < rem) {
			rem -= root | 1;
			root += 2;
		}
	}
	return root >> 1;
}

float math_inverse_sqrt32(float x)
{
	/* This is a variant of the infamous Quake fast inverse square root algorithm.
	 * The accuracy has been improved by tweaking the original constants:
	 *   https://web.archive.org/web/20180709021629/http://rrrola.wz.cz/inv_sqrt.html
	 */
	union {
		float f;
		uint32_t u;
	} y = {x};

	y.u = 0x5F1FFFF9ul - (y.u >> 1);
	return 0.703952253f * y.f * (2.38924456f - x * y.f * y.f);
}

uint32_t math_vector_xy_sq_magnitude(int16_t x, int16_t y)
{
	/* Maximum absolute value of each axis is 2^15
	 * Each axis squared is (2^15 * 2^15) == 2^30
	 * The sum of each axis squared is (2 * 2^30) == 2^31 < 2^32
	 * A uint32_t is therefore sufficient to hold the maximum value.
	 */
	uint32_t x2 = ((int32_t)x * (int32_t)x);
	uint32_t y2 = ((int32_t)y * (int32_t)y);

	return x2 + y2;
}

uint16_t math_vector_xy_magnitude(int16_t x, int16_t y)
{
	return math_sqrt32(math_vector_xy_sq_magnitude(x, y));
}

uint32_t math_vector_xyz_sq_magnitude(int16_t x, int16_t y, int16_t z)
{
	/* Maximum absolute value of each axis is 2^15
	 * Each axis squared is (2^15 * 2^15) == 2^30
	 * The sum of each axis squared is (3 * 2^30) == (1.5 * 2^31) < 2^32
	 * A uint32_t is therefore sufficient to hold the maximum value.
	 */
	uint32_t x2 = ((int32_t)x * (int32_t)x);
	uint32_t y2 = ((int32_t)y * (int32_t)y);
	uint32_t z2 = ((int32_t)z * (int32_t)z);

	return x2 + y2 + z2;
}

uint16_t math_vector_xyz_magnitude(int16_t x, int16_t y, int16_t z)
{
	return math_sqrt32(math_vector_xyz_sq_magnitude(x, y, z));
}

int64_t math_vector_xyz_dot_product(int16_t ax, int16_t ay, int16_t az, int16_t bx, int16_t by,
				    int16_t bz)
{
	/* Maximum absolute value of each axis is 2^15
	 * Each axis squared is (2^15 * 2^15) == 2^30
	 * The sum of each axis squared is (3 * 2^30) == (1.5 * 2^31) > 2^31
	 * Therefore if all the vectors are maxed, we would overflow an int31_t
	 */
	int64_t dx = ax * bx;
	int64_t dy = ay * by;
	int64_t dz = az * bz;

	return dx + dy + dz;
}

int32_t math_vector_xyz_dot_product_fast(int16_t ax, int16_t ay, int16_t az, int16_t bx, int16_t by,
					 int16_t bz)
{
	/* Assume the potential overflow in math_vector_xyz_dot_product doesn't happen */
	int32_t dx = ax * bx;
	int32_t dy = ay * by;
	int32_t dz = az * bz;

	return dx + dy + dz;
}

uint32_t math_bitmask_get_next_bits(uint32_t bitmask, uint8_t start_idx, uint8_t *next_idx,
				    uint8_t num_bits)
{
	uint32_t out = 0;
	uint32_t test;
	uint8_t idx = start_idx;

	/* Can't return more bits than exist */
	num_bits = MIN(num_bits, __builtin_popcount(bitmask));

	while (num_bits) {
		test = (1 << idx);
		if (test & bitmask) {
			out |= test;
			num_bits--;
		}
		idx = (idx + 1) % 32;
	}
	*next_idx = idx;
	return out;
}
