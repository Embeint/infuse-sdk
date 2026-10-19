/*
 * Copyright (c) 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq25798_charger

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

#include "../sensor/bq25798.h"

struct bq25798_charger_config {
	const struct device *parent;
	struct i2c_dt_spec bus;
};

/* Read the requested status registers, NULL pointers are not read */
static int bq25798_charger_get_status_regs(const struct device *dev, uint8_t *status0,
					   uint8_t *status1)
{
	const struct bq25798_charger_config *config = dev->config;
	uint8_t regs[2];
	int rc;

	if (status0 == NULL) {
		return i2c_reg_read_byte_dt(&config->bus, BQ25798_REG_CHARGER_STATUS_1, status1);
	}
	if (status1 == NULL) {
		return i2c_reg_read_byte_dt(&config->bus, BQ25798_REG_CHARGER_STATUS_0, status0);
	}

	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_CHARGER_STATUS_0, regs, sizeof(regs));
	if (rc != 0) {
		return rc;
	}
	*status0 = regs[0];
	*status1 = regs[1];

	return 0;
}

static int bq25798_charger_get_status(const struct device *dev, enum charger_status *status)
{
	uint8_t status0, status1;
	int rc;

	rc = bq25798_charger_get_status_regs(dev, &status0, &status1);
	if (rc != 0) {
		return rc;
	}

	switch (status1 & BQ25798_CHARGER_STATUS_1_CHG_STAT_MASK) {
	case BQ25798_CHARGER_STATUS_1_NOT_CHARGING:
		/* Distinguish no input source from charging being inhibited */
		*status = (status0 & BQ25798_CHARGER_STATUS_0_POWER_GOOD)
				  ? CHARGER_STATUS_NOT_CHARGING
				  : CHARGER_STATUS_DISCHARGING;
		break;
	case BQ25798_CHARGER_STATUS_1_TRICKLE_CHARGE:
	case BQ25798_CHARGER_STATUS_1_PRE_CHARGE:
	case BQ25798_CHARGER_STATUS_1_FAST_CHARGE:
	case BQ25798_CHARGER_STATUS_1_TAPER_CHARGE:
	case BQ25798_CHARGER_STATUS_1_TOP_OFF:
		*status = CHARGER_STATUS_CHARGING;
		break;
	case BQ25798_CHARGER_STATUS_1_TERM_DONE:
		*status = CHARGER_STATUS_FULL;
		break;
	default:
		*status = CHARGER_STATUS_UNKNOWN;
		break;
	}

	return 0;
}

static int bq25798_charger_get_charge_type(const struct device *dev,
					   enum charger_charge_type *charge_type)
{
	uint8_t status1;
	int rc;

	rc = bq25798_charger_get_status_regs(dev, NULL, &status1);
	if (rc != 0) {
		return rc;
	}

	switch (status1 & BQ25798_CHARGER_STATUS_1_CHG_STAT_MASK) {
	case BQ25798_CHARGER_STATUS_1_NOT_CHARGING:
	case BQ25798_CHARGER_STATUS_1_TERM_DONE:
		*charge_type = CHARGER_CHARGE_TYPE_NONE;
		break;
	case BQ25798_CHARGER_STATUS_1_TRICKLE_CHARGE:
	case BQ25798_CHARGER_STATUS_1_PRE_CHARGE:
		*charge_type = CHARGER_CHARGE_TYPE_TRICKLE;
		break;
	case BQ25798_CHARGER_STATUS_1_FAST_CHARGE:
		*charge_type = CHARGER_CHARGE_TYPE_FAST;
		break;
	case BQ25798_CHARGER_STATUS_1_TAPER_CHARGE:
	case BQ25798_CHARGER_STATUS_1_TOP_OFF:
		*charge_type = CHARGER_CHARGE_TYPE_STANDARD;
		break;
	default:
		*charge_type = CHARGER_CHARGE_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

static int bq25798_charger_get_prop(const struct device *dev, charger_prop_t prop,
				    union charger_propval *val)
{
	switch (prop) {
	case CHARGER_PROP_STATUS:
		return bq25798_charger_get_status(dev, &val->status);
	case CHARGER_PROP_CHARGE_TYPE:
		return bq25798_charger_get_charge_type(dev, &val->charge_type);
	default:
		return -ENOTSUP;
	}
}

static int bq25798_charger_set_prop(const struct device *dev, charger_prop_t prop,
				    const union charger_propval *val)
{
	return -ENOTSUP;
}

static int bq25798_charger_charge_enable(const struct device *dev, const bool enable)
{
	return -ENOTSUP;
}

static int bq25798_charger_init(const struct device *dev)
{
	const struct bq25798_charger_config *config = dev->config;

	/* Parent device validates and configures the chip */
	if (!device_is_ready(config->parent)) {
		return -ENODEV;
	}

	return i2c_is_ready_dt(&config->bus) ? 0 : -ENODEV;
}

static DEVICE_API(charger, bq25798_charger_api) = {
	.get_property = bq25798_charger_get_prop,
	.set_property = bq25798_charger_set_prop,
	.charge_enable = bq25798_charger_charge_enable,
};

#define BQ25798_CHARGER_INIT(inst)                                                                 \
	static const struct bq25798_charger_config bq25798_charger_##inst##_config = {             \
		.parent = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                     \
		.bus = I2C_DT_SPEC_GET(DT_PARENT(DT_DRV_INST(inst))),                              \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, bq25798_charger_init, NULL, NULL,                              \
			      &bq25798_charger_##inst##_config, POST_KERNEL,                       \
			      CONFIG_CHARGER_INIT_PRIORITY, &bq25798_charger_api);

DT_INST_FOREACH_STATUS_OKAY(BQ25798_CHARGER_INIT)
