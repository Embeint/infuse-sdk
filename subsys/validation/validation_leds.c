/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <infuse/validation/core.h>
#include <infuse/validation/leds.h>

#define TEST "LED"

int infuse_validation_leds(const struct gpio_dt_spec *leds, uint8_t num_leds, uint8_t flags)
{
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "Testing %d LEDs", num_leds);

	/* Check led initialisation */
	for (int i = 0; i < num_leds; i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) is not ready",
						leds[i].port->name, leds[i].pin, num_leds);
			return -ENODEV;
		}
	}

	/* Power up device */
	for (int i = 0; i < num_leds; i++) {
		rc = pm_device_runtime_get(leds[i].port);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) is not ready",
						leds[i].port->name, leds[i].pin, num_leds);
			goto handle_end_pm;
		}
	}

	/* Setup GPIO pin */
	for (int i = 0; i < num_leds; i++) {
		rc = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) is not ready",
						leds[i].port->name, leds[i].pin, num_leds);
			goto handle_end_gpio;
		}
	}

	/* Toggle LEDs in sequence */
	for (int i = 0; i < num_leds; i++) {
		rc = gpio_pin_set_dt(&leds[i], true);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) failed to set pin",
						leds[i].port->name, leds[i].pin, num_leds);
			goto handle_end_gpio;
		}
		k_sleep(K_MSEC(CONFIG_INFUSE_VALIDATION_LEDS_ACTION_DURATION));
		rc = gpio_pin_set_dt(&leds[i], false);
	}

handle_end_gpio:
	for (int i = 0; i < num_leds; i++) {
		if (gpio_pin_configure_dt(&leds[i], GPIO_DISCONNECTED) < 0) {
			if (rc == 0) {
				VALIDATION_REPORT_ERROR(TEST, "Failed to disconnect LED pin GPIO");
				rc = -EIO;
			}
		}
	}
handle_end_pm:
	for (int i = 0; i < num_leds; i++) {
		if (pm_device_runtime_put(leds[i].port) < 0) {
			if (rc == 0) {
				VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
				rc = -EIO;
			}
		}
	}

	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}

	return rc;
}
