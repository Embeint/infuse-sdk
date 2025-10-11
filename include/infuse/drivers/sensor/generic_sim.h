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
 * @param reset_rc Reset the return codes
 */
void generic_sim_reset(const struct device *dev, bool reset_rc);

/**
 * @brief Configure return value for generic sensor device
 *
 * @param dev Generic simulator sensor device to reset
 * @param resume_rc Return code for @a PM_DEVICE_ACTION_RESUME
 * @param suspend_rc Return code for @a PM_DEVICE_ACTION_SUSPEND
 * @param fetch_rc Return code for @a sensor_sample_fetch
 */
void generic_sim_func_rc(const struct device *dev, int resume_rc, int suspend_rc, int fetch_rc);

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
