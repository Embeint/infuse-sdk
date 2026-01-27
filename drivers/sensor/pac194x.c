/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/pm/device.h>

#include "pac194x.h"

LOG_MODULE_REGISTER(PAC194X, CONFIG_SENSOR_LOG_LEVEL);

static int pac194x_write_cmd(const struct device *dev, uint8_t reg, uint8_t delay_ms)
{
	const struct pac194x_config *config = dev->config;
	uint8_t cmd = reg;
	int rc;

	rc = i2c_write_dt(&config->bus, &cmd, 1);
	if (rc < 0) {
		LOG_DBG("Failed to write %02X register", reg);
	}
	/* All registers change dynamically for up to 1ms after the REFRESH commands */
	if (delay_ms > 0) {
		k_sleep(K_MSEC(delay_ms));
	}
	return rc;
}

static int pac194x_write_u16(const struct device *dev, uint8_t reg, uint16_t reg_val)
{
	const struct pac194x_config *config = dev->config;
	uint8_t regs[3] = {
		reg,
		reg_val >> 8,
		reg_val & 0xFF,
	};
	int rc;

	rc = i2c_write_dt(&config->bus, regs, 3);
	if (rc < 0) {
		LOG_DBG("Failed to write %02X register", reg);
	}
	return rc;
}

static int pac194x_read_n(const struct device *dev, uint8_t reg, uint8_t *buf, uint8_t buf_len)
{
	const struct pac194x_config *config = dev->config;
	int rc;

	rc = i2c_burst_read_dt(&config->bus, reg, buf, buf_len);
	if (rc < 0) {
		LOG_DBG("Failed to read %02X register", reg);
	}
	return rc;
}

static int pac194x_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct pac194x_data *data = dev->data;
	uint32_t acc_count;
	uint8_t values[4];
	int rc;

	ARG_UNUSED(chan);

	/* REFRESH to trigger the single shot sample.
	 * The single shot measurement on one channel is expected to take 5.2 ms.
	 */
	rc = pac194x_write_cmd(dev, PAC194X_REG_REFRESH, 8);
	if (rc < 0) {
		return rc;
	}

	/* REFRESH to load the results into the registers. */
	rc = pac194x_write_cmd(dev, PAC194X_REG_REFRESH, 1);
	if (rc < 0) {
		return rc;
	}

	/* Read number of samples and validate against expected count */
	rc = pac194x_read_n(dev, PAC194X_REG_ACC_COUNT, values, 4);
	if (rc < 0) {
		return rc;
	}
	acc_count = sys_get_be32(values);
	if (acc_count != 8) {
		LOG_WRN("Unexpected sample count: %d", acc_count);
		return -EINVAL;
	}

	/* Read out the 8 sample averages */
	rc = pac194x_read_n(dev, PAC194X_REG_VBUS_0_AVG, values, 2);
	if (rc < 0) {
		return rc;
	}
	data->v_bus = sys_get_be16(values);

	rc = pac194x_read_n(dev, PAC194X_REG_VSENSE_0_AVG, values, 2);
	if (rc < 0) {
		return rc;
	}
	data->v_sense = sys_get_be16(values);

	LOG_DBG("  VBUS AVG raw: %d", data->v_bus);
	LOG_DBG("VSENSE AVG raw: %d", data->v_sense);
	return 0;
}

static int pac194x_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	const struct pac194x_config *config = dev->config;
	struct pac194x_data *data = dev->data;
	int64_t val_micro;

	switch (chan) {
	case SENSOR_CHAN_VOLTAGE:
		val_micro = 9000000 * (int64_t)data->v_bus >> config->vbus_shift;
		break;
	case SENSOR_CHAN_CURRENT:
		val_micro = (config->full_scale_current_microamps * (int64_t)data->v_sense) >>
			    config->vsense_shift;
		break;
	default:
		return -ENOTSUP;
	}

	return sensor_value_from_micro(val, val_micro);
}

static int wait_until_ready(const struct device *dev)
{
	const struct pac194x_config *config = dev->config;
	uint8_t regs[3] = {0};
	int elapsed = 0;
	int rc = -EIO;

	while (elapsed <= 150) {
		/* Wait for next poll */
		k_sleep(K_MSEC(10));
		elapsed += 10;

		/* Read the ID bytes */
		rc = pac194x_read_n(dev, PAC194X_REG_PRODUCT_ID, regs, 3);
		if (rc < 0) {
			continue;
		}
		if (regs[0] != config->product_id) {
			rc = -EIO;
			continue;
		}
		LOG_DBG("Manu: 0x%02X Part: 0x%02X Rev: 0x%02X", regs[1], regs[0], regs[2]);
		break;
	}
	if (rc == 0) {
		LOG_DBG("Ready after %d ms", elapsed);
	} else {
		LOG_ERR("Failed to read ID registers");
	}
	return rc;
}

static int pac194x_power_up(const struct device *dev)
{
	const struct pac194x_config *config = dev->config;
	uint16_t ctrl = PAC194X_CTRL_SLOW_ALERT_INPUT | PAC194X_CTRL_GPIO_ALERT_INPUT |
			PAC194X_CTRL_MODE_SINGLE_SHOT_8X | config->disabled_channels;
	int rc = 0;

	if (config->power_down_gpio.port) {
		/* Move out of power down state */
		gpio_pin_configure_dt(&config->power_down_gpio, GPIO_OUTPUT_INACTIVE);
	}

	/* Wait until communications are working */
	rc = wait_until_ready(dev);
	if (rc < 0) {
		goto end;
	}

	/* Write the control register */
	rc = pac194x_write_u16(dev, PAC194X_REG_CTRL, ctrl);
	if (rc < 0) {
		goto end;
	}

	/* Configure the full scale ranges */
	rc = pac194x_write_u16(dev, PAC194X_REG_NEG_PWR_FSR, config->fsr_config);
	if (rc < 0) {
		goto end;
	}

	/* REFRESH to update internal registers */
	rc = pac194x_write_cmd(dev, PAC194X_REG_REFRESH, 1);
	if (rc < 0) {
		goto end;
	}
end:
	if ((rc < 0) && config->power_down_gpio.port) {
		/* Resume failed, put back into sleep mode */
		gpio_pin_set_dt(&config->power_down_gpio, 1);
	}
	return rc;
}

static void pac194x_suspend(const struct device *dev)
{
	const struct pac194x_config *config = dev->config;

	/* Without a power down pin, we just sit in the sleep state from the single shot sampling */
	if (config->power_down_gpio.port) {
		gpio_pin_set_dt(&config->power_down_gpio, 1);
	}
}

static int pac194x_resume(const struct device *dev)
{
	const struct pac194x_config *config = dev->config;

	if (!config->power_down_gpio.port) {
		/* Already in the correct state from power up */
		return 0;
	}

	/* Run power up sequence */
	return pac194x_power_up(dev);
}

static int pac194x_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct pac194x_config *config = dev->config;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		/* Ensure device is ready to talk to us */
		rc = pac194x_power_up(dev);
		if (rc < 0) {
			LOG_DBG("Failed to power up");
			return -EIO;
		}
		/* Return to low power mode */
		pac194x_suspend(dev);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		if (config->power_down_gpio.port) {
			gpio_pin_configure_dt(&config->power_down_gpio, GPIO_DISCONNECTED);
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		/* Return to low power mode */
		pac194x_suspend(dev);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = pac194x_resume(dev);
		break;
	}
	return rc;
}

static int pac194x_init(const struct device *dev)
{
	const struct pac194x_config *const config = dev->config;

	if (!device_is_ready(config->bus.bus)) {
		return -ENODEV;
	}

	if (config->power_down_gpio.port) {
		if (!gpio_is_ready_dt(&config->power_down_gpio)) {
			return -ENODEV;
		}
	}

	return pm_device_driver_init(dev, pac194x_pm_control);
}

static DEVICE_API(sensor, pac194x_driver_api) = {
	.sample_fetch = pac194x_sample_fetch,
	.channel_get = pac194x_channel_get,
};

/* FSC = 0.1V / R_sense */
#define INST_FULL_SCALE_CURRENT(inst)                                                              \
	((1000000 * 100) / DT_INST_PROP(inst, sense_resistor_milli_ohms))

#define PAC194X_DRIVER_INIT(inst, type, _disabled_channels)                                        \
	static const struct pac194x_config drv_config_##type##inst = {                             \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.power_down_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, power_down_gpios, {0}),          \
		.full_scale_current_microamps = INST_FULL_SCALE_CURRENT(inst),                     \
		.fsr_config = (DT_INST_ENUM_IDX(inst, fsr_vbus_channel_1) << 6) |                  \
			      (DT_INST_ENUM_IDX(inst, fsr_vsense_channel_1) << 14),                \
		.disabled_channels = _disabled_channels,                                           \
		.vbus_shift = DT_INST_ENUM_IDX(inst, fsr_vbus_channel_1) == 1 ? 15 : 16,           \
		.vsense_shift = DT_INST_ENUM_IDX(inst, fsr_vsense_channel_1) == 1 ? 15 : 16,       \
		.product_id = PAC194X_PRODUCT_ID_##type,                                           \
	};                                                                                         \
	static struct pac194x_data drv_data_##type##inst;                                          \
	PM_DEVICE_DT_INST_DEFINE(inst, pac194x_pm_control);                                        \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, &pac194x_init, PM_DEVICE_DT_INST_GET(inst),             \
				     &drv_data_##type##inst, &drv_config_##type##inst,             \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &pac194x_driver_api);

#define DT_DRV_COMPAT microchip_pac1941_1
DT_INST_FOREACH_STATUS_OKAY_VARGS(PAC194X_DRIVER_INIT, PAC1941_1, 0x70)
#undef DT_DRV_COMPAT
