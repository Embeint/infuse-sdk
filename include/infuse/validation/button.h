/**
 * @file
 * @brief button gpio press validation
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BUTTON_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BUTTON_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_button Button validation API
 * @{
 */

enum {
	VALIDATION_BUTTON_MODE_TRIGGER = BIT(0),
	VALIDATION_BUTTON_MODE_RELEASE = BIT(1),
	VALIDATION_BUTTON_MODE_BOTH =
		VALIDATION_BUTTON_MODE_TRIGGER | VALIDATION_BUTTON_MODE_RELEASE,
};

int infuse_validation_button(const struct gpio_dt_spec *button, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BUTTON_H_ */
