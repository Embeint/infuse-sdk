/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_ENV_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_ENV_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_env ENV validation API
 * @{
 */

enum {
	/** Validate that sensor powers up */
	VALIDATION_ENV_POWER_UP = 0,
	/** Rigorous driver behavioural tests */
	VALIDATION_ENV_DRIVER = BIT(0),
};

/**
 * @brief Validate the behaviour of an environmental sensor
 *
 * @param dev Environmental sensor
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_env(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_ENV_H_ */
