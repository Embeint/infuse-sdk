/**
 * @file
 * @brief Emulated IMU driver control
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_EMUL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_EMUL_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMU emul API
 * @defgroup imu_emul_apis emulated IMU APIs
 * @{
 */

/**
 * @brief Configure the accelerometer output samples
 *
 * @param dev Emulated IMU device
 * @param x_ratio X axis base = 1G * this
 * @param y_ratio Y axis base = 1G * this
 * @param z_ratio Z axis base = 1G * this
 * @param axis_noise Random number in range +- this is added to each axis
 */
void imu_emul_accelerometer_data_configure(const struct device *dev, float x_ratio, float y_ratio,
					   float z_ratio, uint16_t axis_noise);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_IMU_EMUL_H_ */
