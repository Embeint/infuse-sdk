/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT maxim_max17260

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor/battery.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include "max17260.h"

struct max17260_config {
	struct i2c_dt_spec bus;
	int32_t ocv_lookup_table[BATTERY_OCV_TABLE_LEN];
	uint32_t charge_capacity_microamp_hours;
	enum battery_chemistry chemistry;
	uint16_t sense_resistor;
};

LOG_MODULE_REGISTER(max17260, CONFIG_MAX17260_LOG_LEVEL);

static int reg_to_uv(uint16_t reg)
{
	/* LSB = 78.125 uV */
	return 625 * (int)reg / 8;
}

static int reg_to_ua(const struct device *dev, int16_t reg)
{
	const struct max17260_config *config = dev->config;

	/* LSB = 1.5625μV/RSENSE */
	return (25000 / 16) * (int)reg / config->sense_resistor;
}

static int reg_read(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct max17260_config *config = dev->config;

	return i2c_burst_read_dt(&config->bus, reg, (uint8_t *)val, sizeof(*val));
}

static int max17260_get_prop(const struct device *dev, fuel_gauge_prop_t prop,
			     union fuel_gauge_prop_val *val)
{
	const struct max17260_config *config = dev->config;
	uint16_t unsigned_reg = 0;
	int16_t signed_reg = 0;
	int rc = -ENOTSUP;

	rc = pm_device_runtime_get(config->bus.bus);
	if (rc < 0) {
		return rc;
	}

	switch (prop) {
	case FUEL_GAUGE_FULL_CHARGE_CAPACITY:
		if (config->charge_capacity_microamp_hours > 0) {
			val->full_charge_capacity = config->charge_capacity_microamp_hours;
			rc = 0;
		}
		break;
	case FUEL_GAUGE_DESIGN_CAPACITY:
		if (config->charge_capacity_microamp_hours > 0) {
			val->full_charge_capacity = config->charge_capacity_microamp_hours / 1000;
			rc = 0;
		}
		break;
	case FUEL_GAUGE_VOLTAGE:
		rc = reg_read(dev, MAX17260_REG_VOLTAGE_CELL, &unsigned_reg);
		val->voltage = reg_to_uv(unsigned_reg);
		break;
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
	case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE:
		/* Use manual OCV lookup table instead of the internal SoC tracking */
		if (config->ocv_lookup_table[0] != -1) {
			rc = reg_read(dev, MAX17260_REG_VOLTAGE_CELL, &unsigned_reg);
			val->relative_state_of_charge =
				battery_soc_lookup(config->ocv_lookup_table,
						   reg_to_uv(unsigned_reg)) /
				1000;
		}
		break;
	case FUEL_GAUGE_CURRENT:
		rc = reg_read(dev, MAX17260_REG_CURRENT, &signed_reg);
		val->current = reg_to_ua(dev, signed_reg);
		break;
	}
	(void)pm_device_runtime_put(config->bus.bus);
	return rc;
}

static int max17260_init(const struct device *dev)
{
	const struct max17260_config *config = dev->config;
	uint16_t status;
	int rc;

	if (!i2c_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	/* Read an arbitrary register to ensure device is reachable */
	rc = pm_device_runtime_get(config->bus.bus);
	if (rc < 0) {
		return rc;
	}
	rc = reg_read(dev, MAX17260_REG_STATUS, &status);
	if (rc < 0) {
		goto end;
	}
end:
	(void)pm_device_runtime_put(config->bus.bus);
	return rc;
}

static const struct fuel_gauge_driver_api max17260_api = {
	.get_property = max17260_get_prop,
	.set_property = NULL,
	.get_buffer_property = NULL,
	.battery_cutoff = NULL,
};

#define MAX17260_INIT(inst)                                                                        \
	static const struct max17260_config max17260_##inst##_config = {                           \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.ocv_lookup_table =                                                                \
			BATTERY_OCV_TABLE_DT_GET(DT_DRV_INST(inst), ocv_capacity_table_0),         \
		.charge_capacity_microamp_hours =                                                  \
			DT_INST_PROP_OR(inst, charge_full_design_microamp_hours, 0),               \
		.chemistry = BATTERY_CHEMISTRY_DT_GET(inst),                                       \
		.sense_resistor = DT_INST_PROP(inst, sense_resistor_milli_ohms),                   \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, max17260_init, NULL, NULL, &max17260_##inst##_config,          \
			      POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &max17260_api);

DT_INST_FOREACH_STATUS_OKAY(MAX17260_INIT)
