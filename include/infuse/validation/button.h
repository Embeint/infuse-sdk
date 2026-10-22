/**
 * @file
 * @brief button gpio press validation
 * @copyright 2024 Embeint Holdings Pty Ltd
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
 * @ingroup infuse_validation
 * @{
 */

enum {
	/** Wait for the button to transition to the active state */
	VALIDATION_BUTTON_MODE_TRIGGER = BIT(0),
	/** Wait for the button to transition to the inactive state */
	VALIDATION_BUTTON_MODE_RELEASE = BIT(1),
	/** Wait for both trigger and release transitions */
	VALIDATION_BUTTON_MODE_BOTH =
		VALIDATION_BUTTON_MODE_TRIGGER | VALIDATION_BUTTON_MODE_RELEASE,
};

/**
 * @brief Validate that button transitions can be observed
 *
 * Waits up to @kconfig{CONFIG_INFUSE_VALIDATION_BUTTON_ACTION_TIMEOUT} seconds for the
 * requested button transitions to occur.
 *
 * @param button Button GPIO specification
 * @param flags Transitions to wait for (VALIDATION_BUTTON_MODE_*)
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_button(const struct gpio_dt_spec *button, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_BUTTON_H_ */
