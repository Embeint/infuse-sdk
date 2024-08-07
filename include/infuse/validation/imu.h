/**
 * @file
 * @brief
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_IMU_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_IMU_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <infuse/drivers/imu.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_imu IMU validation API
 * @{
 */

enum {
	/** Validate that sensor powers up */
	VALIDATION_IMU_POWER_UP = 0,
	/** Rigorous driver behavioural tests */
	VALIDATION_IMU_DRIVER = BIT(0),
};

/**
 * @brief Validate the behaviour of a device implementing the IMU API
 *
 * @param dev IMU device
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_imu(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_IMU_H_ */
