/**
 * @file
 * @brief Timezone helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_TIME_TIMEZONE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_TIME_TIMEZONE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief timezone API
 * @defgroup timezone_apis timezone APIs
 * @{
 */

/**
 * @brief Calculate the approximate UTC timezone of a location
 *
 * This function splits the earth into 15 degree longitude chunks to approximate the
 * timezone without requiring any information other than the location. Due to the
 * vagaries of actual time-zones, the true timezone offset may differ from this value
 * by up to 3 hours (Western China).
 *
 * @param longitude Longitude (scaled by 1e7)
 *
 * @return int8_t Approximate timezone offset in hours
 */
static inline int8_t utc_timezone_location_approximate(int32_t longitude)
{
	int32_t rounding = longitude > 0 ? 75000000 : -75000000;

	return (longitude + rounding) / 150000000;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_TIME_TIMEZONE_H_ */
