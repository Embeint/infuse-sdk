/**
 * @file
 * @brief Generic simulated sensor
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_GENERIC_SIM_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_GENERIC_SIM_H_

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset all channels back to unconfigured
 *
 * @param dev Generic simulator sensor device to reset
 */
void generic_sim_reset(const struct device *dev);

/**
 * @brief Set the value to be returned for a given channel
 *
 * @param dev Generic simulator sensor device
 * @param chan Channel to configure
 * @param val Value to return when channel queried
 *
 * @retval 0 on success
 * @retval -EINVAL on invalid channel
 */
int generic_sim_channel_set(const struct device *dev, enum sensor_channel chan,
			    struct sensor_value val);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_SENSOR_GENERIC_SIM_H_ */
