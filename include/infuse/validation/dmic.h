/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DMIC_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DMIC_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_dmic Digital microphone validation API
 * @{
 */

enum {
	/** Validate that microphone powers up */
	VALIDATION_DMIC_POWER_UP = 0,
	/** 1 second statistical analysis of data */
	VALIDATION_DMIC_STATISTICAL_SAMPLE = BIT(0),
};

/**
 * @brief Validate the behaviour of an digital microphone
 *
 * @param dev Digital microphone
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_dmic(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_DMIC_H_ */
