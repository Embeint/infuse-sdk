/**
 * @file
 * @brief Control power state of device based on battery SoC
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * Enable and disable the power state of a device based on the reported
 * battery state of charge percentage.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_SOC_DEVICE_CONTROL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_SOC_DEVICE_CONTROL_H_

#include <zephyr/sys/slist.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief soc_device_control API
 * @defgroup soc_device_control_apis SoC Device Control API
 * @{
 */

struct soc_device_control_state {
	/** @cond INTERNAL_HIDDEN */
	sys_snode_t _node;
	/** @endcond */
	/* Device that is being controlled based on charge state */
	const struct device *device;
	/* Enable device when charge state reaches at least this value */
	uint8_t soc_enable;
	/* Disable device when charge state falls below this value */
	uint8_t soc_disable;
	/** @cond INTERNAL_HIDDEN */
	bool requested;
	/** @endcond */
};

/**
 * @brief Register a device to be controlled
 *
 * @note @a state object must remain valid in memory until @ref soc_device_control_unregister is
 * called
 *
 * @param state State object containing device and thresholds
 *
 * @retval 0 On success
 * @retval -ENODEV If @a state does not contain a valid device
 * @retval -EINVAL On invalid @a soc_enable or @a soc_disable configuration
 */
int soc_device_control_register(struct soc_device_control_state *state);

/**
 * @brief Unregister a controlled device
 *
 * @note Unregistering a device releases any active power state claim
 *
 * @param state State object previously provided to @ref soc_device_control_register
 *
 * @retval true Device unregistered
 * @retval false Device not previously registered
 */
bool soc_device_control_unregister(struct soc_device_control_state *state);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_SOC_DEVICE_CONTROL_H_ */
