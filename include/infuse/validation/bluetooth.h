/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BLUETOOTH_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BLUETOOTH_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_bluetooth Bluetooth validation
 * @ingroup infuse_validation
 * @{
 */

enum {
	/** Validate that the Bluetooth stack can be enabled */
	VALIDATION_BLUETOOTH_INIT = 0,
	/** Transmit an advertising packet */
	VALIDATION_BLUETOOTH_ADV_TX = BIT(0),
};

/**
 * @brief Validate the behaviour of the Bluetooth stack
 *
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_bluetooth(uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BLUETOOTH_H_ */
