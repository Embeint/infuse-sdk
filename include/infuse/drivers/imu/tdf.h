/**
 * @file
 * @brief IMU TDF helpers
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_TDF_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_TDF_H_

#include <stdint.h>

#include <infuse/tdf/definitions.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMU TDF helpers
 * @defgroup imu_tdf_apis IMU TDF helpers
 * @ingroup infuse_imu_apis
 * @{
 */

/**
 * @brief Get TDF ID to use for given accelerometer full scale range
 *
 * @param range Maximum data range in Gs
 *
 * @return Appropriate TDF ID
 */
static inline uint16_t tdf_id_from_accelerometer_range(uint8_t range)
{
	switch (range) {
	case 2:
		return TDF_ACC_2G;
	case 4:
		return TDF_ACC_4G;
	case 8:
		return TDF_ACC_8G;
	default:
		return TDF_ACC_16G;
	}
}

/**
 * @brief Get TDF ID to use for given gyroscope full scale range
 *
 * @param range Maximum data range in DPS
 *
 * @return Appropriate TDF ID
 */
static inline uint16_t tdf_id_from_gyroscope_range(uint16_t range)
{
	switch (range) {
	case 125:
		return TDF_GYR_125DPS;
	case 250:
		return TDF_GYR_250DPS;
	case 500:
		return TDF_GYR_500DPS;
	case 1000:
		return TDF_GYR_1000DPS;
	default:
		return TDF_GYR_2000DPS;
	}
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_TDF_H_ */
