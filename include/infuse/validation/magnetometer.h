/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_MAGNETOMETER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_MAGNETOMETER_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_magnetometer Magnetometer validation API
 * @ingroup infuse_validation
 * @{
 */

enum {
	/** Validate that sensor powers up */
	VALIDATION_MAGNETOMETER_POWER_UP = 0,
	/** Measure the magnetic field on all supported axes */
	VALIDATION_MAGNETOMETER_FIELD = BIT(0),
};

/**
 * @brief Validate the behaviour of a magnetometer or hall effect sensor
 *
 * @param dev Magnetometer
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_magnetometer(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_MAGNETOMETER_H_ */
