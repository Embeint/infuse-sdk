/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DIE_TEMP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DIE_TEMP_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_die temp Die temperature validation API
 * @{
 */

enum {
	/** Validate that sensor powers up */
	VALIDATION_DIE_TEMP_POWER_UP = 0,
	/** Measure internal die temperature */
	VALIDATION_DIE_TEMP_TEMPERATURE = BIT(0),
};

/**
 * @brief Validate the behaviour of a die temperature sensor
 *
 * @param dev Environmental sensor
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_die_temperature(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DIE_TEMP_H_ */
