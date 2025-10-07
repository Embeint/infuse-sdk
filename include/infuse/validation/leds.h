/**
 * @file
 * @brief leds validation
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details Validate LED hardware. Check leds can be addressed.
 * Note: Does not validate the LEDs actually light up
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LEDS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LEDS_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_leds LEDs validation APIs
 * @{
 */

enum {
	VALIDATION_LEDS_OBSERVE_ONLY = 0,
};

/**
 * @brief Validate GPIO LED devices
 *
 * @param leds leds gpio specs to test
 * @param num_leds number of LEDs in @p leds
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_leds_gpio(const struct gpio_dt_spec *leds, uint8_t num_leds, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_LEDS_H_ */
