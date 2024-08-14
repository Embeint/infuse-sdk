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
#include <infuse/validation/pwr.h>

#define TEST "PWR"

static int validate_battery(const struct device *dev, uint8_t flags)
{
	struct sensor_value voltage;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "BATTERY=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		return -ENODEV;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		return rc;
	}

	if (flags & VALIDATION_PWR_DRIVER) {
		/* Give voltage time to settle */
		k_sleep(K_MSEC(10));

		/* Trigger the sample */
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "sensor_sample_fetch (%d)", rc);
			goto driver_end;
		}

		/* Retrieve and display the channel readings */
		rc = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "%11s: %d.%03d V", "Voltage", voltage.val1,
					       voltage.val2 / 1000);
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Voltage get failed (%d)", rc);
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
	return 0;
}

static int validate_charger(const struct device *dev, uint8_t flags)
{
	struct sensor_value current;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "CHARGER=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		return -ENODEV;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		return rc;
	}

	if (flags & VALIDATION_PWR_DRIVER) { /* Trigger the sample */
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "sensor_sample_fetch (%d)", rc);
			goto driver_end;
		}

		/* Retrieve and display the channel readings */
		rc = sensor_channel_get(dev, SENSOR_CHAN_CURRENT, &current);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "%11s: %d.%03d A", "Current", current.val1,
					       current.val2 / 1000);
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Current get failed (%d)", rc);
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
	return 0;
}

int infuse_validation_pwr(const struct device *battery, const struct device *charger, uint8_t flags)
{
	int rc = 0;

	if (battery != NULL) {
		if (validate_battery(battery, flags) < 0) {
			rc = -EINVAL;
		}
	}
	if (charger != NULL) {
		if (validate_charger(charger, flags) < 0) {
			rc = -EINVAL;
		}
	}
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
