/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <infuse/validation/core.h>
#include <infuse/validation/leds.h>

#define TEST "LED_PWM"

static int leds_toggle(const struct pwm_dt_spec *leds, uint8_t num_leds)
{
	int rc = 0;

	/* Toggle LEDs in sequence, fully on then off */
	for (int i = 0; i < num_leds; i++) {
		rc = pwm_set_pulse_dt(&leds[i], leds[i].period);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST,
						"Device=%s:%d (LED #%d) failed to enable (%d)",
						leds[i].dev->name, leds[i].channel, i, rc);
			break;
		}
		k_sleep(K_MSEC(CONFIG_INFUSE_VALIDATION_LEDS_ACTION_DURATION));
		rc = pwm_set_pulse_dt(&leds[i], 0);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST,
						"Device=%s:%d (LED #%d) failed to disable (%d)",
						leds[i].dev->name, leds[i].channel, i, rc);
			break;
		}
	}

	return rc;
}

int infuse_validation_leds_pwm(const struct pwm_dt_spec *leds, uint8_t num_leds, uint8_t flags)
{
	uint8_t powered_up = 0;
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "Testing %d LEDs", num_leds);

	/* Check PWM initialisation */
	for (int i = 0; i < num_leds; i++) {
		if (!pwm_is_ready_dt(&leds[i])) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) is not ready",
						leds[i].dev->name, leds[i].channel, i);
			return -ENODEV;
		}
	}

	/* Power up device */
	for (int i = 0; i < num_leds; i++) {
		rc = pm_device_runtime_get(leds[i].dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Device=%s:%d (LED #%d) failed to power up",
						leds[i].dev->name, leds[i].channel, i);
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
