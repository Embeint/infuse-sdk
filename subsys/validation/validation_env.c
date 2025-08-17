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
#include <infuse/validation/env.h>

#define TEST "ENV"

int infuse_validation_env(const struct device *dev, uint8_t flags)
{
	double temp, press, hum;
	struct sensor_value val;
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
		rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &val);
		if (rc == 0) {
			temp = sensor_value_to_float(&val);
			VALIDATION_REPORT_VALUE(TEST, "TEMPERATURE", "%.03f", temp);
		} else if (rc == -ENOTSUP) {
			/* Try the die temperature for internal measurements */
			rc = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &val);
			if (rc == 0) {
				temp = sensor_value_to_float(&val);
				VALIDATION_REPORT_VALUE(TEST, "TEMPERATURE", "%.03f", temp);
			} else if (rc == -ENOTSUP) {
				/* Unsupported channel */
				rc = 0;
			} else {
				VALIDATION_REPORT_ERROR(TEST, "Temperature get failed (%d)", rc);
			}
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Temperature get failed (%d)", rc);
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &val);
		if (rc == 0) {
			press = sensor_value_to_float(&val);
			VALIDATION_REPORT_VALUE(TEST, "PRESSURE", "%.03f", press);
		} else if (rc == -ENOTSUP) {
			/* Unsupported channel */
			rc = 0;
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Pressure get failed (%d)", rc);
		}
		rc = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &val);
		if (rc == 0) {
			hum = sensor_value_to_float(&val);
			VALIDATION_REPORT_VALUE(TEST, "HUMIDITY", "%.03f", hum);
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
