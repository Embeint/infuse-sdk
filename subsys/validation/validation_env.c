/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <infuse/validation/core.h>
#include <infuse/validation/env.h>

#define TEST "ENV"

int infuse_validation_env(const struct device *dev, uint8_t flags)
{
	struct sensor_value temp, press, hum;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		rc = -ENODEV;
		goto test_end;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	if (flags & VALIDATION_ENV_DRIVER) { /* Trigger the sample */
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "sensor_sample_fetch (%d)", rc);
			goto driver_end;
		}

		/* Retrieve and display the channel readings */
		rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "%11s: %d.%03d deg", "Temperature", temp.val1,
					       temp.val2 / 1000);
		} else if (rc == -ENOTSUP) {
			/* Unsupported channel */
			rc = 0;
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Temperature get failed (%d)", rc);
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "%11s: %d.%03d kPa", "Pressure", press.val1,
					       press.val2 / 1000);
		} else if (rc == -ENOTSUP) {
			/* Unsupported channel */
			rc = 0;
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Pressure get failed (%d)", rc);
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "%11s: %d.%02d%%", "Humidity", hum.val1,
					       hum.val2 / 10000);
		} else if (rc == -ENOTSUP) {
			/* Unsupported channel */
			rc = 0;
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Humidity get failed (%d)", rc);
		}
	}
driver_end:

	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
