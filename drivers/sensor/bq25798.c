/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT ti_bq25798

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>

#include "bq25798.h"

#define MPPT_DISABLE 0xFF

struct bq25798_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec en_gpio;
	struct gpio_dt_spec int_gpio;
	float ts_rt1;
	float ts_rt1_rt2_ratio;
	float ntc_beta;
	uint16_t ntc_r0;
	uint16_t ntc_t0;
	uint16_t v_sys_min;
	uint16_t v_in_dpm;
	uint16_t input_current_limit;
	uint8_t mppt_ratio;
	uint8_t acdrv_en_cfg;
	uint8_t vac_ovp;
};

struct bq25798_data {
	struct gpio_callback int_cb;
	struct k_sem int_sem;
	/* ADC registers are not contiguous, 0x31 to 0x41 */
	struct {
		uint16_t i_bus;
		uint16_t i_bat;
		uint16_t v_bus;
		uint16_t v_ac1;
		uint16_t v_ac2;
		uint16_t v_bat;
		uint16_t v_sys;
		uint16_t ts;
		uint16_t tdie;
	} __packed adc_regs;
	float ts_log_divisor;
};

LOG_MODULE_REGISTER(bq25798, CONFIG_SENSOR_LOG_LEVEL);

static int bq25798_reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct bq25798_config *config = dev->config;
	uint8_t buf[2] = {
		reg,
		val,
	};

	return i2c_write_dt(&config->bus, buf, 2);
}

static const char *const status_str[] = {
	"Not Charging",      "Trickle Charge", "Pre-Charge",    "Fast Charge (CC)",
	"Taper Charge (CV)", "Reserved",       "Top-off Timer", "Charge Termination",
};

static int bq25798_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct bq25798_config *config = dev->config;
	struct bq25798_data *data = dev->data;
	uint8_t reg;
	int rc;

#ifdef CONFIG_BQ25798_FETCH_STATUS_CHECKS
	uint8_t status[5];
	uint8_t chg_stat;

	/* Status checks */
	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_CHARGER_STATUS_0, status, 5);
	if (rc == 0) {
		LOG_HEXDUMP_DBG(status, sizeof(status), "Charger status registers");
		/* Charger status */
		chg_stat = (status[1] & BQ25798_CHARGER_STATUS_1_CHG_STAT_MASK) >>
			   BQ25798_CHARGER_STATUS_1_CHG_STAT_OFF;
		LOG_INF("Charger status: %s", status_str[chg_stat]);
		/* VBUS present but not good */
		if (status[0] & BQ25798_CHARGER_STATUS_0_VBUS_PRESENT) {
			bool ac1 = status[0] & BQ25798_CHARGER_STATUS_0_AC1_PRESENT;
			bool ac2 = status[0] & BQ25798_CHARGER_STATUS_0_AC2_PRESENT;
			bool pg = status[0] & BQ25798_CHARGER_STATUS_0_POWER_GOOD;

			LOG_INF("VBUS:%s%s (power %s)", ac1 ? " AC1 present" : "",
				ac2 ? " AC2 present" : "", pg ? "good" : "bad");
		}
		/* Thermal regulation */
		if (status[2] & BQ25798_CHARGER_STATUS_2_TREG) {
			LOG_WRN("Thermal regulation");
		}
		/* Thermistor status */
		if (status[4] & BQ25798_CHARGER_STATUS_4_TS_COLD) {
			LOG_WRN("Thermistor cold");
		} else if (status[4] & BQ25798_CHARGER_STATUS_4_TS_COOL) {
			LOG_INF("Thermistor cool");
		} else if (status[4] & BQ25798_CHARGER_STATUS_4_TS_WARM) {
			LOG_INF("Thermistor warm");
		} else if (status[4] & BQ25798_CHARGER_STATUS_4_TS_HOT) {
			LOG_WRN("Thermistor hot");
		}
	}
	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_FAULT_STATUS_0, status, 2);
	if (rc == 0) {
		LOG_HEXDUMP_DBG(status, 2, "Fault status registers");
		if (status[0] & BQ25798_CHARGER_FAULT_0_VAC1_OVP) {
			LOG_WRN("VAC%d over-voltage", 1);
		}
		if (status[0] & BQ25798_CHARGER_FAULT_0_VAC2_OVP) {
			LOG_WRN("VAC%d over-voltage", 2);
		}
	}

#endif /* CONFIG_BQ25798_FETCH_STATUS_CHECKS */

#ifdef CONFIG_BQ25798_FETCH_POOR_SOURCE_RETRY
	rc = i2c_reg_read_byte_dt(&config->bus, BQ25798_REG_CHARGER_CONTROL_0, &reg);
	if ((rc == 0) && (reg & BQ25798_CHARGER_CONTROL_0_EN_HIZ)) {
		/* Reset EN_HIZ to force source qualification retry */
		LOG_INF("Forcing source requalification");
		reg &= ~BQ25798_CHARGER_CONTROL_0_EN_HIZ;
		(void)bq25798_reg_write(dev, BQ25798_REG_CHARGER_CONTROL_0, reg);
	}
#endif /* CONFIG_BQ25798_FETCH_POOR_SOURCE_RETRY */

	/* Clear interrupts from other sources */
	(void)k_sem_take(&data->int_sem, K_NO_WAIT);

	/* Enable the one-shot measurement */
	reg = BQ25798_ADC_CONTROL_EN | BQ25798_ADC_CONTROL_ONE_SHOT | BQ25798_ADC_CONTROL_15_BIT;
	rc = bq25798_reg_write(dev, BQ25798_REG_ADC_CONTROL, reg);
	if (rc != 0) {
		LOG_ERR("Failed to enable ADC (%d)", rc);
		return rc;
	}

	/* Wait for the interrupt signifying completion */
	LOG_DBG("Waiting for ADC completion");
	rc = k_sem_take(&data->int_sem, K_MSEC(500));
	if (rc != 0) {
		/* Manually check the register to see if it was just an interrupt problem */
		rc = i2c_reg_read_byte_dt(&config->bus, BQ25798_REG_CHARGER_FLAG_2, &reg);
		if ((rc == 0) && (reg & BQ25798_CHARGER_FLAG_2_ADC_DONE)) {
			LOG_WRN("ADC interrupt did not fire");
		} else {
			LOG_ERR("ADC sampling failed");
			return -EIO;
		}
	}

	/* Read the ADC results */
	return i2c_burst_read_dt(&config->bus, BQ25798_REG_IBUS_ADC, (void *)&data->adc_regs,
				 sizeof(data->adc_regs));
}

static int bq25798_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	const struct bq25798_config *config = dev->config;
	struct bq25798_data *data = dev->data;
	uint32_t val_unsigned;
	int32_t val_signed;
	float val_float;
	int rc;

	switch (chan) {
	case SENSOR_CHAN_VOLTAGE:
	case SENSOR_CHAN_GAUGE_VOLTAGE:
		val_unsigned = sys_be16_to_cpu(data->adc_regs.v_bat);
		/* 1mV step size */
		rc = sensor_value_from_milli(val, val_unsigned);
		break;
	case SENSOR_CHAN_CURRENT:
	case SENSOR_CHAN_GAUGE_AVG_CURRENT:
		val_signed = (int16_t)sys_be16_to_cpu(data->adc_regs.i_bat);
		/* 1mA step size */
		rc = sensor_value_from_milli(val, val_signed);
		break;
	case SENSOR_CHAN_GAUGE_TEMP: {
		float rth, inv_ts;

		/* Inverted TS reading */
		inv_ts = 1.0f / (sys_be16_to_cpu(data->adc_regs.ts) * 0.0976563f * 0.01f);
		/* Equivalent resistance seen by ADC */
		rth = config->ts_rt1 / (inv_ts - 1 - config->ts_rt1_rt2_ratio);
		/* Convert to temperature (Kelvin) */
		val_float = config->ntc_beta / logf(rth / data->ts_log_divisor);
		/* Report in degrees celcius */
		rc = sensor_value_from_float(val, val_float - 273.15f);
		break;
	}
	case SENSOR_CHAN_DIE_TEMP:
		val_signed = (int16_t)sys_be16_to_cpu(data->adc_regs.tdie);
		/* 0.5 deg step size */
		rc = sensor_value_from_milli(val, 500 * val_signed);
	default:
		rc = -ENOTSUP;
	}
	return rc;
}

static void bq25798_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bq25798_data *data = CONTAINER_OF(cb, struct bq25798_data, int_cb);

	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_DBG("CB");
	k_sem_give(&data->int_sem);
}

static int bq25798_init(const struct device *dev)
{
	const struct bq25798_config *config = dev->config;
	struct bq25798_data *data = dev->data;
	uint8_t reg = 0;
	int rc;

	if (!i2c_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	/* Precompute constant divisor */
	data->ts_log_divisor = config->ntc_r0 * expf(-config->ntc_beta / config->ntc_t0);

	/* Initialise data structures */
	gpio_init_callback(&data->int_cb, bq25798_gpio_callback, BIT(config->int_gpio.pin));
	if (gpio_add_callback(config->int_gpio.port, &data->int_cb) < 0) {
		LOG_DBG("Could not set gpio callback");
		return -EIO;
	}
	k_sem_init(&data->int_sem, 0, 1);

	/* Configure GPIOs */
	gpio_pin_configure_dt(&config->en_gpio, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);

	/* Validate communications */
	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_PART_INFO, &reg, 1);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_PART_INFO, "read", rc);
		return rc;
	}
	if (reg != BQ25798_PART_INFO_EXPECTED) {
		LOG_ERR("Unexpected PART_INFO (%02X != %02X)", reg, BQ25798_PART_INFO_EXPECTED);
		return -ENODEV;
	}

	/* Reset to default register values */
	rc = bq25798_reg_write(dev, BQ25798_REG_TERM_CONTROL, BQ25798_TERM_CONTROL_REG_RST);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_TERM_CONTROL, "write", rc);
		return rc;
	}

	/* Configure limits */
	rc = bq25798_reg_write(dev, BQ25798_REG_MIN_SYS_VOLTAGE, (config->v_sys_min - 2500) / 250);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_MIN_SYS_VOLTAGE, "write", rc);
		return rc;
	}
	rc = bq25798_reg_write(dev, BQ25798_REG_INPUT_VOLTAGE_LIM, config->v_in_dpm / 100);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_INPUT_VOLTAGE_LIM, "write", rc);
		return rc;
	}
	rc = bq25798_reg_write(dev, BQ25798_REG_INPUT_CURRENT_LIM,
			       config->input_current_limit / 10);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_INPUT_CURRENT_LIM, "write", rc);
		return rc;
	}

	/* Disable the watchdog, configure over-voltage protection */
	reg = config->vac_ovp | BQ25798_CHARGER_CONTROL_1_WD_RST |
	      BQ25798_CHARGER_CONTROL_1_WD_DISABLE;
	rc = bq25798_reg_write(dev, BQ25798_REG_CHARGER_CONTROL_1, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_CONTROL_1, "write", rc);
		return rc;
	}

	/* Check FET detection */
	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_CHARGER_STATUS_3, &reg, 1);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_STATUS_3, "read", rc);
		return rc;
	}
	if ((config->acdrv_en_cfg & BQ25798_CHARGER_CONTROL_4_EN_ACDRV2) &&
	    !(reg & BQ25798_CHARGER_STATUS_3_ACRB2)) {
		LOG_WRN("ACFET%d-RBFET%d requested but not present", 2, 2);
	}
	if ((config->acdrv_en_cfg & BQ25798_CHARGER_CONTROL_4_EN_ACDRV1) &&
	    !(reg & BQ25798_CHARGER_STATUS_3_ACRB1)) {
		LOG_WRN("ACFET%d-RBFET%d requested but not present", 1, 1);
	}

	/* Configure ACFETs */
	rc = i2c_burst_read_dt(&config->bus, BQ25798_REG_CHARGER_CONTROL_4, &reg, 1);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_CONTROL_4, "read", rc);
		return rc;
	}
	reg &= ~(BQ25798_CHARGER_CONTROL_4_EN_ACDRV1 | BQ25798_CHARGER_CONTROL_4_EN_ACDRV2);
	reg |= config->acdrv_en_cfg;
	rc = bq25798_reg_write(dev, BQ25798_REG_CHARGER_CONTROL_4, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_CONTROL_4, "write", rc);
		return rc;
	}

	/* Configure MPPT */
	if (config->mppt_ratio == MPPT_DISABLE) {
		reg = BQ25798_MPPT_CONTROL_MPPT_DISABLE;
	} else {
		reg = BQ25798_MPPT_CONTROL_MPPT_ENABLE | BQ25798_MPPT_CONTROL_VOC_PERIOD_30S |
		      BQ25798_MPPT_CONTROL_VOC_DELAY_300MS |
		      (config->mppt_ratio << BQ25798_MPPT_CONTROL_RATIO_OFFSET);
	}
	rc = bq25798_reg_write(dev, BQ25798_REG_MPPT_CONTROL, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_MPPT_CONTROL, "write", rc);
		return rc;
	}

	/* Enable battery current measurement */
	reg = BQ25798_CHARGER_CONTROL_5_EN_IBAT | BQ25798_CHARGER_CONTROL_5_IBAT_REG_DISABLE |
	      BQ25798_CHARGER_CONTROL_5_EN_IINDPM | BQ25798_CHARGER_CONTROL_5_EN_EXTILIM;
	rc = bq25798_reg_write(dev, BQ25798_REG_CHARGER_CONTROL_5, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_CONTROL_5, "write", rc);
		return rc;
	}

	/* Disable unused ADC channels to speed up conversion.
	 * Note that VAC1, VAC2 and VBUS seem to be required for normal operation of the device.
	 */
	reg = BQ25798_ADC_FUNC_DISABLE_0_IBUS | BQ25798_ADC_FUNC_DISABLE_0_VSYS;
	rc = bq25798_reg_write(dev, BQ25798_REG_ADC_FUNC_DISABLE_0, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_ADC_FUNC_DISABLE_0, "write", rc);
		return rc;
	}
	reg = BQ25798_ADC_FUNC_DISABLE_1_DP | BQ25798_ADC_FUNC_DISABLE_1_DM;
	rc = bq25798_reg_write(dev, BQ25798_REG_ADC_FUNC_DISABLE_1, reg);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_ADC_FUNC_DISABLE_1, "write", rc);
		return rc;
	}

	/* Disable temperature related interrupts (transient interrupts when sampling ADC) */
	rc = bq25798_reg_write(dev, BQ25798_REG_CHARGER_MASK_3, 0x0F);
	if (rc != 0) {
		LOG_ERR("Reg 0x%02X %s error (%d)", BQ25798_REG_CHARGER_MASK_3, "write", rc);
		return rc;
	}

	return 0;
}

static const struct sensor_driver_api bq25798_driver_api = {
	.sample_fetch = bq25798_sample_fetch,
	.channel_get = bq25798_channel_get,
};

#define BQ25798_INIT(inst)                                                                         \
	static const struct bq25798_config bq25798_##inst##_config = {                             \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.en_gpio = GPIO_DT_SPEC_INST_GET(inst, en_gpios),                                  \
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                                \
		.ts_rt1 = DT_INST_PROP(inst, ts_rt1),                                              \
		.ts_rt1_rt2_ratio =                                                                \
			((float)DT_INST_PROP(inst, ts_rt1)) / DT_INST_PROP(inst, ts_rt2),          \
		.ntc_beta = DT_INST_PROP(inst, ntc_beta),                                          \
		.ntc_r0 = DT_INST_PROP(inst, ntc_r0),                                              \
		.ntc_t0 = DT_INST_PROP(inst, ntc_t0),                                              \
		.v_sys_min = DT_INST_PROP(inst, v_sys_min),                                        \
		.v_in_dpm = DT_INST_PROP(inst, v_in_dpm),                                          \
		.input_current_limit = DT_INST_PROP(inst, input_current_limit),                    \
		.mppt_ratio = DT_INST_ENUM_IDX_OR(inst, mppt_ratio, MPPT_DISABLE),                 \
		.acdrv_en_cfg =                                                                    \
			(DT_INST_PROP(inst, acdrv1_en) ? BQ25798_CHARGER_CONTROL_4_EN_ACDRV1       \
						       : 0) |                                      \
			(DT_INST_PROP(inst, acdrv2_en) ? BQ25798_CHARGER_CONTROL_4_EN_ACDRV2 : 0), \
		.vac_ovp = (DT_INST_ENUM_IDX(inst, vac_ovp) << 4),                                 \
	};                                                                                         \
	static struct bq25798_data bq25798_##inst##_data;                                          \
	DEVICE_DT_INST_DEFINE(inst, bq25798_init, NULL, &bq25798_##inst##_data,                    \
			      &bq25798_##inst##_config, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,  \
			      &bq25798_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BQ25798_INIT)
