/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <infuse/validation/core.h>
#include <infuse/validation/die_temp.h>

#define TEST "DIE"

int infuse_validation_die_temperature(const struct device *dev, uint8_t flags)
{
	struct sensor_value val;
	double temp;
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		rc = -ENODEV;
		goto test_end;
	}

	if (flags & VALIDATION_DIE_TEMP_TEMPERATURE) {
		/* Fetch a sample */
		rc = sensor_sample_fetch(dev);
		if (rc != 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to fetch sample");
			goto test_end;
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &val);
		if (rc != 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to retrieve reading");
			goto test_end;
		}

		temp = sensor_value_to_float(&val);
		VALIDATION_REPORT_VALUE(TEST, "TEMPERATURE", "%.03f", temp);
	}

test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
