/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_PWR_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_PWR_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_pwr Power validation API
 * @{
 */

enum {
	/** Validate that fuel-gauge power up */
	VALIDATION_PWR_POWER_UP = 0,
	/** Rigorous driver behavioural tests */
	VALIDATION_PWR_DRIVER = BIT(0),
};

/**
 * @brief Validate the behaviour of a power sensors
 *
 * @param fuel_gauge Device that implements the fuel-gauge API
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_pwr(const struct device *fuel_gauge, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_PWR_H_ */
