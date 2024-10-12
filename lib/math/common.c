/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

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

uint16_t math_vector_xy_magnitude(int16_t x, int16_t y)
{
	/* Maximum absolute value of each axis is 2^15
	 * Each axis squared is (2^15 * 2^15) == 2^30
	 * The sum of each axis squared is (2 * 2^30) == 2^31 < 2^32
	 * A uint32_t is therefore sufficient to hold the maximum value.
	 */
	uint32_t x2 = ((int32_t)x * (int32_t)x);
	uint32_t y2 = ((int32_t)y * (int32_t)y);
	uint32_t sq_magnitude = x2 + y2;

	return math_sqrt32(sq_magnitude);
}

uint16_t math_vector_xyz_magnitude(int16_t x, int16_t y, int16_t z)
{
	/* Maximum absolute value of each axis is 2^15
	 * Each axis squared is (2^15 * 2^15) == 2^30
	 * The sum of each axis squared is (3 * 2^30) == (1.5 * 2^31) < 2^32
	 * A uint32_t is therefore sufficient to hold the maximum value.
	 */
	uint32_t x2 = ((int32_t)x * (int32_t)x);
	uint32_t y2 = ((int32_t)y * (int32_t)y);
	uint32_t z2 = ((int32_t)z * (int32_t)z);
	uint32_t sq_magnitude = x2 + y2 + z2;

	return math_sqrt32(sq_magnitude);
}
