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
#include <infuse/validation/light.h>

#define TEST "LIGHT"

int infuse_validation_light(const struct device *dev, uint8_t flags)
{
	struct sensor_value val;
	double lux, ir;
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

	if (flags & VALIDATION_LIGHT_ILLUMINANCE) {
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "sensor_sample_fetch (%d)", rc);
			goto driver_end;
		}

		/* Illuminance is required */
		rc = sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &val);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Illuminance get failed (%d)", rc);
			goto driver_end;
		}
		lux = sensor_value_to_float(&val);
		VALIDATION_REPORT_VALUE(TEST, "ILLUMINANCE", "%.03f", lux);
		if (lux < 0.0) {
			VALIDATION_REPORT_ERROR(TEST, "Negative illuminance");
			rc = -EINVAL;
			goto driver_end;
		}

		/* Infrared is optional */
		rc = sensor_channel_get(dev, SENSOR_CHAN_IR, &val);
		if (rc == 0) {
			ir = sensor_value_to_float(&val);
			VALIDATION_REPORT_VALUE(TEST, "IR", "%.03f", ir);
		} else if (rc == -ENOTSUP) {
			/* Unsupported channel */
			rc = 0;
		} else {
			VALIDATION_REPORT_ERROR(TEST, "IR get failed (%d)", rc);
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
