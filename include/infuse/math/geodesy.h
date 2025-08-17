/**
 * @file
 * @brief Earth geodesic functions
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_MATH_GEODESY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_MATH_GEODESY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief geodesy API
 * @defgroup geodesy_apis geodesy APIs
 * @{
 */

/* Coordinate point */
struct geodesy_coordinate {
	/* Latitude, scaled by 1e7 */
	int32_t latitude;
	/* Longitude, scaled by 1e7 */
	int32_t longitude;
};

/**
 * @brief Calculate the great-circle (shortest) distance between two locations
 *
 * @param a First location
 * @param b Second location
 *
 * @return uint32_t Distance between location in meters
 */
uint32_t geodesy_great_circle_distance(struct geodesy_coordinate a, struct geodesy_coordinate b);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_MATH_GEODESY_H_ */
