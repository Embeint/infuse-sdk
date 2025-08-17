/**
 * @file
 * @brief Common math functionality
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_COMMON_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_COMMON_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup infuse_math Infuse-IoT Math libraries
 * @{
 */

/**
 * @brief Absolute value
 *
 * @param x Value to absolute
 *
 * @return Absolute value of x
 */
#define math_abs(x) ((x) < 0 ? -(x) : (x))

/**
 * @brief Compute square root of unsigned 16 bit number
 *
 * @param x Value to square root
 *
 * @return uint8_t Square root of number
 */
uint8_t math_sqrt16(uint16_t x);

/**
 * @brief Compute square root of unsigned 32 bit number
 *
 * @param x Value to square root
 *
 * @return uint16_t Square root of number
 */
uint16_t math_sqrt32(uint32_t x);

/**
 * @brief Compute square root of unsigned 64 bit number
 *
 * @param x Value to square root
 *
 * @return uint16_t Square root of number
 */
uint32_t math_sqrt64(uint64_t x);

/**
 * @brief Fast inverse square root
 *
 * @param x Value to compute inverse square root of
 *
 * @return float Inverse square root (1 / sqrt(x))
 */
float math_inverse_sqrt32(float x);

/**
 * @brief Compute the squared magnitude of an XY vector
 *
 * @param x X component of vector
 * @param y Y component of vector
 *
 * @return uint16_t Squared magnitude of vector (x*x + y*y)
 */
uint32_t math_vector_xy_sq_magnitude(int16_t x, int16_t y);

/**
 * @brief Compute the magnitude of an XY vector
 *
 * @param x X component of vector
 * @param y Y component of vector
 *
 * @return uint16_t Magnitude of vector sqrt(x*x + y*y)
 */
uint16_t math_vector_xy_magnitude(int16_t x, int16_t y);

/**
 * @brief Compute the squared magnitude of an XYZ vector
 *
 * @param x X component of vector
 * @param y Y component of vector
 * @param z Z component of vector
 *
 * @return uint16_t Squared magnitude of vector (x*x + y*y + z*z)
 */
uint32_t math_vector_xyz_sq_magnitude(int16_t x, int16_t y, int16_t z);

/**
 * @brief Compute the magnitude of an XYZ vector
 *
 * @param x X component of vector
 * @param y Y component of vector
 * @param z Z component of vector
 *
 * @return uint16_t Magnitude of vector sqrt(x*x + y*y + z*z)
 */
uint16_t math_vector_xyz_magnitude(int16_t x, int16_t y, int16_t z);

/**
 * @brief Compute the dot product of two XYZ vectors
 *
 * The maximum value of the output is 1.5 * (2 ** 31), when all inputs are INT16_MIN.
 *
 * @param ax A vector, X component
 * @param ay A vector, Y component
 * @param az A vector, Z component
 * @param bx B vector, X component
 * @param by B vector, Y component
 * @param bz B vector, Z component
 *
 * @return int64_t Dot product of the two vectors
 */
int64_t math_vector_xyz_dot_product(int16_t ax, int16_t ay, int16_t az, int16_t bx, int16_t by,
				    int16_t bz);

/**
 * @brief Compute the dot product of two XYZ vectors
 *
 * A faster variant of @ref math_vector_xyz_dot_product that assumes the dot product fits
 * in an int32_t, which will be true for most, but not all, inputs.
 *
 * @param ax A vector, X component
 * @param ay A vector, Y component
 * @param az A vector, Z component
 * @param bx B vector, X component
 * @param by B vector, Y component
 * @param bz B vector, Z component
 *
 * @return int32_t Dot product of the two vectors
 */
int32_t math_vector_xyz_dot_product_fast(int16_t ax, int16_t ay, int16_t az, int16_t bx, int16_t by,
					 int16_t bz);

/**
 * @brief Get the next N bits of a bitmask, with rollover
 *
 * @param bitmask Bitmask to iterate over
 * @param start_idx Index of first bit to check
 * @param next_idx Index of first bit to check on next iteration
 * @param num_bits Number of bits to return
 *
 * @return uint32_t N bits from @a bitmask
 */
uint32_t math_bitmask_get_next_bits(uint32_t bitmask, uint8_t start_idx, uint8_t *next_idx,
				    uint8_t num_bits);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_COMMON_H_ */
