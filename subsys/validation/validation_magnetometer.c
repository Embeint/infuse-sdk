/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <infuse/validation/core.h>
#include <infuse/validation/magnetometer.h>

#define TEST "MAGN"

/* Returns 1 if the axis was measured, 0 if unsupported, -errno on error */
static int axis_get(const struct device *dev, enum sensor_channel chan, double *field)
{
	struct sensor_value val;
	int rc;

	rc = sensor_channel_get(dev, chan, &val);
	if (rc == -ENOTSUP) {
		return 0;
	} else if (rc < 0) {
		return rc;
	}
	*field = sensor_value_to_float(&val);
	return 1;
}

int infuse_validation_magnetometer(const struct device *dev, uint8_t flags)
{
	int measured[3];
	double field[3];
	uint8_t supported = 0;
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

	if (flags & VALIDATION_MAGNETOMETER_FIELD) {
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "sensor_sample_fetch (%d)", rc);
			goto driver_end;
		}

		/* Sensors may only measure a subset of axes (e.g. hall effect sensors) */
		measured[0] = axis_get(dev, SENSOR_CHAN_MAGN_X, &field[0]);
		measured[1] = axis_get(dev, SENSOR_CHAN_MAGN_Y, &field[1]);
		measured[2] = axis_get(dev, SENSOR_CHAN_MAGN_Z, &field[2]);
		for (int i = 0; i < ARRAY_SIZE(measured); i++) {
			if (measured[i] < 0) {
				VALIDATION_REPORT_ERROR(TEST, "Axis %c get failed (%d)", 'X' + i,
							measured[i]);
				rc = measured[i];
				goto driver_end;
			}
			supported += measured[i];
		}
		if (measured[0]) {
			VALIDATION_REPORT_VALUE(TEST, "FIELD_X", "%.03f", field[0]);
		}
		if (measured[1]) {
			VALIDATION_REPORT_VALUE(TEST, "FIELD_Y", "%.03f", field[1]);
		}
		if (measured[2]) {
			VALIDATION_REPORT_VALUE(TEST, "FIELD_Z", "%.03f", field[2]);
		}

		/* At least one axis is required */
		if (supported == 0) {
			VALIDATION_REPORT_ERROR(TEST, "No axes supported");
			rc = -ENOTSUP;
			goto driver_end;
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
