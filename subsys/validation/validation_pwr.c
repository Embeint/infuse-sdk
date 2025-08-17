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
#include <zephyr/drivers/fuel_gauge.h>
#include <infuse/validation/core.h>
#include <infuse/validation/pwr.h>

int infuse_validation_pwr(const struct device *dev, uint8_t flags)
{
	const char *test = "N/A";
	union fuel_gauge_prop_val val;
	int rc = 0;

	if (POPCOUNT(flags) > 1) {
		test = "PWR";
	} else if (flags & VALIDATION_PWR_BATTERY_VOLTAGE) {
		test = "BAT_V";
	} else if (flags & VALIDATION_PWR_BATTERY_SOC) {
		test = "BAT_%";
	} else if (flags & VALIDATION_PWR_BATTERY_CURRENT) {
		test = "BAT_A";
	}

	VALIDATION_REPORT_INFO(test, "BATTERY=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(test, "Device not ready");
		return -ENODEV;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(test, "pm_device_runtime_get (%d)", rc);
		return rc;
	}

	if (flags & VALIDATION_PWR_BATTERY_VOLTAGE) {
		double voltage;

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_VOLTAGE, &val);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(test, "Voltage get failed (%d)", rc);
			goto driver_end;
		}
		voltage = (double)val.voltage / 1e6;
		VALIDATION_REPORT_VALUE(test, "VOLTAGE", "%.03f", voltage);
	}
	if (flags & VALIDATION_PWR_BATTERY_SOC) {
		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &val);
		if ((rc < 0) && (rc != -ENOTSUP)) {
			VALIDATION_REPORT_ERROR(test, "SoC get failed (%d)", rc);
			goto driver_end;
		}
		if (rc == -ENOTSUP) {
			rc = 0;
		} else {
			VALIDATION_REPORT_VALUE(test, "SOC", "%d", val.relative_state_of_charge);
		}
	}
	if (flags & VALIDATION_PWR_BATTERY_CURRENT) {
		double current;

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_CURRENT, &val);
		if ((rc < 0) && (rc != -ENOTSUP)) {
			VALIDATION_REPORT_ERROR(test, "Charge current get failed (%d)", rc);
			goto driver_end;
		}
		if (rc == -ENOTSUP) {
			rc = 0;
		} else {
			current = (double)val.current / 1e6;
			VALIDATION_REPORT_VALUE(test, "CURRENT", "%.06f", current);
		}
	}
	if (flags & VALIDATION_PWR_BATTERY_TEMPERATURE) {
		double temperature;

		rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_TEMPERATURE, &val);
		if ((rc < 0) && (rc != -ENOTSUP)) {
			VALIDATION_REPORT_ERROR(test, "Temperature get failed (%d)", rc);
			goto driver_end;
		}
		if (rc == -ENOTSUP) {
			rc = 0;
		} else {
			temperature = (double)val.temperature / 10.0;
			VALIDATION_REPORT_VALUE(test, "TEMPERATURE", "%.01f", temperature - 273.0);
		}
	}

driver_end:
	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(test, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
	if (rc == 0) {
		VALIDATION_REPORT_PASS(test, "PASSED");
	}
	return rc;
}
