/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT maxim_max17260

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor/battery.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include <infuse/drivers/fuel_gauge_custom_prop.h>

#include "max17260.h"

#define MAX17260_HIB_CFG_DEFAULT 0x870C

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

static int reg_write(const struct device *dev, uint8_t reg, uint16_t val)
{
	const struct max17260_config *config = dev->config;

	return i2c_burst_write_dt(&config->bus, reg, (uint8_t *)&val, sizeof(val));
}

static int reg_read(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct max17260_config *config = dev->config;

	return i2c_burst_read_dt(&config->bus, reg, (uint8_t *)val, sizeof(*val));
}

static int max17260_change_hibernation_mode(const struct device *dev, bool enable)
{
	const struct max17260_config *config = dev->config;
	int rc = 0;
	uint16_t reg;

	/* Claim bus */
	rc = pm_device_runtime_get(config->bus.bus);
	if (rc < 0) {
		LOG_ERR("pm_device_runtime_get failed (%s)", config->bus.bus->name);
		return rc;
	}

	/* Get current config */
	rc = reg_read(dev, MAX17260_REG_HIB_CFG, &reg);
	if (rc < 0) {
		LOG_ERR("Failed to check hibernation configuration");
		goto end;
	}

	if (!!(reg & MAX17260_HIB_CFG_EN_HIB) == enable) {
		/* Configuration already set, return */
		goto end;
	}

	rc = reg_write(dev, MAX17260_REG_HIB_CFG, enable ? MAX17260_HIB_CFG_DEFAULT : 0x0000);
	if (rc < 0) {
		LOG_ERR("Failed to write to Hibernation cfg register");
		goto end;
	}

	if (!enable) {
		/* Wake up fuel gauge. Requires ~50ms and ~100ms after each of the following
		 * commands (determined experimentally) to actually work.
		 */
		rc = reg_write(dev, MAX17260_REG_CMD, MAX17260_CMD_SOFT_WAKEUP);
		if (rc < 0) {
			LOG_ERR("Failed to write Command (wakeup)");
			goto end;
		}

		rc = reg_write(dev, MAX17260_REG_CMD, MAX17260_CMD_CLEAR);
		if (rc < 0) {
			LOG_ERR("Failed to write Command (command clear)");
			goto end;
		}
	}
end:
	(void)pm_device_runtime_put(config->bus.bus);
	return rc;
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

static int max17260_set_prop(const struct device *dev, fuel_gauge_prop_t prop,
			     union fuel_gauge_prop_val val)
{
	int rc;

	switch (prop) {
	case FUEL_GAUGE_HIBERNATION_EN:
		rc = max17260_change_hibernation_mode(dev, !!val.sbs_mode);
		break;
	default:
		rc = -ENOSYS;
	}
	return rc;
}

#ifdef CONFIG_PM_DEVICE
static int max17260_shutdown_enter(const struct device *dev)
{
	const struct max17260_config *config = dev->config;
	uint16_t reg;
	int rc;

	/* Claim bus */
	rc = pm_device_runtime_get(config->bus.bus);
	if (rc < 0) {
		return rc;
	}
	/* Move to active mode (faster shutdown response) */
	rc = reg_write(dev, MAX17260_REG_HIB_CFG, 0x0000);
	if (rc < 0) {
		goto end;
	}

	/* Set shutdown timer to expire soon.
	 * The minimum timeout is 45 seconds, so write the counter
	 * to ~40 seconds so that it times out in ~5 seconds.
	 * Counter LSB is 1.4 seconds.
	 */
	rc = reg_write(dev, MAX17260_REG_SHUTDOWN_TIMER, 0x001E);
	if (rc < 0) {
		goto end;
	}

	/* Get the current state of the config register */
	rc = reg_read(dev, MAX17260_REG_CONFIG, &reg);
	if (rc < 0) {
		goto end;
	}

	/* Set the shutdown bit and write it back */
	reg |= MAX17260_CONFIG_SHUTDOWN;
	rc = reg_write(dev, MAX17260_REG_CONFIG, reg);
end:
	/* Release bus */
	(void)pm_device_runtime_put(config->bus.bus);
	return rc;
}
#endif /* CONFIG_PM_DEVICE */

static int max17260_shutdown_exit(const struct device *dev)
{
	const struct max17260_config *config = dev->config;
	uint16_t reg;
	int rc;

	/* Claim bus */
	rc = pm_device_runtime_get(config->bus.bus);
	if (rc < 0) {
		return rc;
	}

	/* First thing to do is try and read the CONFIG register.
	 * If this succeeds and the SHUTDOWN bit is set, we have not yet shutdown.
	 */
	rc = reg_read(dev, MAX17260_REG_CONFIG, &reg);
	if ((rc == 0) && (reg & MAX17260_CONFIG_SHUTDOWN)) {
		LOG_DBG("Cancelling pending shutdown");
		/* Cancel the pending shutdown and return */
		reg &= ~MAX17260_CONFIG_SHUTDOWN;
		rc = reg_write(dev, MAX17260_REG_CONFIG, reg);

		/* Aborting shutdown does not restore hibernation config. Override to default */
		if (rc == 0) {
			rc = reg_write(dev, MAX17260_REG_HIB_CFG, MAX17260_HIB_CFG_DEFAULT);
		}
		goto end;
	}

	/* Experimentally, the fuel gauge takes about 400ms total before data is ready.
	 * It starts responding to I2C transactions after ~5ms.
	 * However the responses to those transactions can be invalid for up to 50ms.
	 * Simply waiting 200ms before checking registers skips all the complexity, giving a
	 * more robust implementation in less code.
	 */
	k_timepoint_t end = sys_timepoint_calc(K_MSEC(1000));

	LOG_DBG("Waiting for data ready");
	k_sleep(K_MSEC(200));

	while (1) {
		/* Read FSTAT register */
		rc = reg_read(dev, MAX17260_REG_F_STAT, &reg);
		if ((rc == 0) && !(reg & MAX17260_F_STAT_DATA_NOT_READY)) {
			LOG_DBG("Data ready");
			goto end;
		}
		if (sys_timepoint_expired(end)) {
			break;
		}
		k_sleep(K_MSEC(25));
	}
	rc = -EINVAL;

end:
	/* Release bus */
	(void)pm_device_runtime_put(config->bus.bus);
	return rc;
}

#ifdef CONFIG_PM_DEVICE
static int max17260_pm_control(const struct device *dev, enum pm_device_action action)
{
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		/* Shutdown mode reduces the power consumption from 5uA to 0.5uA,
		 * but the fuel-gauge loses all internal state. It should only be
		 * used in very specific circumstances (shipping modes, etc).
		 */
		rc = max17260_shutdown_enter(dev);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = max17260_shutdown_exit(dev);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
	case PM_DEVICE_ACTION_TURN_ON:
		break;
	default:
		return -ENOTSUP;
	}
	return rc;
}
#endif /* CONFIG_PM_DEVICE */

static int max17260_init(const struct device *dev)
{
	const struct max17260_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	/* Ensure device is not in shutdown mode */
	return max17260_shutdown_exit(dev);
}

static const struct fuel_gauge_driver_api max17260_api = {
	.get_property = max17260_get_prop,
	.set_property = max17260_set_prop,
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
	PM_DEVICE_DT_INST_DEFINE(inst, max17260_pm_control);                                       \
	DEVICE_DT_INST_DEFINE(inst, max17260_init, PM_DEVICE_DT_INST_GET(inst), NULL,              \
			      &max17260_##inst##_config, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
			      &max17260_api);

DT_INST_FOREACH_STATUS_OKAY(MAX17260_INIT)
