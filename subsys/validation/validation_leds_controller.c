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

static int leds_toggle(const struct led_dt_spec *leds, uint8_t num_leds)
{
	int rc = 0;

	/* Toggle LEDs in sequence */
	for (int i = 0; i < num_leds; i++) {
		rc = led_on_dt(&leds[i]);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s (LED #%d) failed to enable",
						leds[i].dev->name, num_leds);
			break;
		}
		k_sleep(K_MSEC(CONFIG_INFUSE_VALIDATION_LEDS_ACTION_DURATION));
		rc = led_off_dt(&leds[i]);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s (LED #%d) failed to disable",
						leds[i].dev->name, num_leds);
			break;
		}
	}

	return rc;
}

int infuse_validation_leds_controller(const struct led_dt_spec *leds, uint8_t num_leds,
				      uint8_t flags)
{
	uint8_t powered_up = 0;
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "Testing %d LEDs", num_leds);

	/* Check led initialisation */
	for (int i = 0; i < num_leds; i++) {
		if (!led_is_ready_dt(&leds[i])) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s (LED #%d) is not ready",
						leds[i].dev->name, num_leds);
			return -ENODEV;
		}
	}

	/* Power up device */
	for (int i = 0; i < num_leds; i++) {
		rc = pm_device_runtime_get(leds[i].dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s (LED #%d) is not ready",
						leds[i].dev->name, num_leds);
			goto handle_end_pm;
		}
		powered_up += 1;
	}

	/* Run the toggle test */
	rc = leds_toggle(leds, num_leds);

handle_end_pm:
	for (int i = 0; i < powered_up; i++) {
		if (pm_device_runtime_put(leds[i].dev) < 0) {
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
