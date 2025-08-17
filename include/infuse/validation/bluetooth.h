/**
 * @file
 * @copyright 2024 Embeint Inc
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
 * @{
 */

enum {
	VALIDATION_BLUETOOTH_INIT = 0,
	VALIDATION_BLUETOOTH_ADV_TX = BIT(0),
};

int infuse_validation_bluetooth(uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BLUETOOTH_H_ */
