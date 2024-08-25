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
#include <zephyr/drivers/fuel_gauge.h>
#include <infuse/validation/core.h>
#include <infuse/validation/pwr.h>

#define TEST "PWR"

int infuse_validation_pwr(const struct device *dev, uint8_t flags)
{
	int rc = 0;

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
		union fuel_gauge_prop_val val;

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_VOLTAGE, &val);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Voltage get failed (%d)", rc);
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "%11s: %d mV", "Voltage", val.voltage / 1000);

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &val);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "SoC get failed (%d)", rc);
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "%11s: %d %%", "SoC", val.relative_state_of_charge);

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_CURRENT, &val);
		if ((rc < 0) && (rc != -ENOTSUP)) {
			VALIDATION_REPORT_ERROR(TEST, "Charge current get failed (%d)", rc);
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "%11s: %d uA", "Current", val.current);
		rc = 0;
	}

driver_end:
	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
