/**
 * @file
 * @brief Common math functionality
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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
 * @brief Compute the magnitude of an XY vector
 *
 * @param x X component of vector
 * @param y Y component of vector
 *
 * @return uint16_t Magnitude of vector sqrt(x*x + y*y)
 */
uint16_t math_vector_xy_magnitude(int16_t x, int16_t y);

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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_COMMON_H_ */
