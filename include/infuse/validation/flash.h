/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_FLASH_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_FLASH_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_flash Flash validation API
 * @{
 */

enum {
	/** Validate that flash powers up */
	VALIDATION_FLASH_POWER_UP = 0,
	/** Rigorous driver behavioural tests */
	VALIDATION_FLASH_DRIVER = BIT(0),
	/** Perform a full chip erase */
	VALIDATION_FLASH_CHIP_ERASE = BIT(1),
};

/**
 * @brief Validate the behaviour of external flash
 *
 * @param dev Flash device
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_flash(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_FLASH_H_ */
