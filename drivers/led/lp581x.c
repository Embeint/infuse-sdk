/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/pm/device.h>

#include "lp581x.h"

struct lp581x_config {
	struct i2c_dt_spec bus;
	uint8_t out0_current;
	uint8_t out1_current;
	uint8_t out2_current;
	uint8_t out3_current;
	uint8_t fade_duration_idx;
	uint8_t exponential_fading;
	uint8_t num_leds;
};

LOG_MODULE_REGISTER(lp5815, CONFIG_LED_LOG_LEVEL);

static int lp581x_led_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct lp581x_config *config = dev->config;
	uint8_t reg;
	int rc;

	if (led >= config->num_leds) {
		return -EINVAL;
	}

	reg = (value * 0xFF) / LED_BRIGHTNESS_MAX;
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_OUT0_MANUAL_PWM + led, reg);
	if (rc < 0) {
		LOG_DBG("Failed to write brightness to led %d (%d)", led, rc);
		return rc;
	}
	return 0;
}

static int lp581x_enable(const struct device *dev)
{
	const struct lp581x_config *config = dev->config;
	uint8_t leds_all = LP581X_CONFIG1_OUT0_EN | LP581X_CONFIG1_OUT1_EN | LP581X_CONFIG1_OUT2_EN;
	uint8_t fade_cfg;
	int rc;

	/* Toggle SDA 8 times while SCL is held high to exit shutdown mode */
	rc = i2c_sda_toggle(config->bus.bus, 8);
	if (rc < 0) {
		LOG_DBG("Failed to toggle SDA (%d)", rc);
		return rc;
	}
	/* Give the device a chance to move out of shutdown mode */
	k_sleep(K_MSEC(1));

	/* Move to normal mode with instant blinking disabled */
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_CHIP_EN,
				   LP581X_CHIP_EN_CHIP_ENABLE | LP581X_CHIP_EN_INSTABLINK_DISABLE);
	if (rc < 0) {
		LOG_DBG("Failed to enable (%d)", rc);
		return rc;
	}

	/* Re-write the maximum output current registers */
	if ((i2c_reg_write_byte_dt(&config->bus, LP581X_REG_OUT0_DC, config->out0_current) < 0) ||
	    (i2c_reg_write_byte_dt(&config->bus, LP581X_REG_OUT1_DC, config->out1_current) < 0) ||
	    (i2c_reg_write_byte_dt(&config->bus, LP581X_REG_OUT2_DC, config->out2_current) < 0) ||
	    ((config->num_leds == 4) &&
	     i2c_reg_write_byte_dt(&config->bus, LP581X_REG_OUT3_DC, config->out3_current) < 0)) {
		LOG_DBG("Failed to configure Dot Current (%d)", rc);
		return -EIO;
	}
	LOG_DBG("Maximum Currents: %02X %02X %02X %02X", config->out0_current, config->out1_current,
		config->out2_current, config->out3_current);

	/* Enable all LED channels by default */
	if (config->num_leds == 4) {
		leds_all |= LP581X_CONFIG1_OUT3_EN;
	}
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG1, leds_all);
	if (rc < 0) {
		LOG_DBG("Failed to enable LEDs (%d)", rc);
		return rc;
	}

	/* Manual fading control */
	if (config->fade_duration_idx == 0) {
		fade_cfg = 0x00;
	} else {
		fade_cfg =
			((config->num_leds == 4) ? 0x0F : 0x07) | (config->fade_duration_idx << 4);
	}
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG2, fade_cfg);
	if (rc < 0) {
		LOG_DBG("Failed to configure fading (%d)", rc);
		return rc;
	}

	/* Exponential fading curve */
	if (config->exponential_fading) {
		fade_cfg = (config->num_leds == 4) ? 0xF0 : 0x70;
	} else {
		fade_cfg = 0x00;
	}
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG3, fade_cfg);
	if (rc < 0) {
		LOG_DBG("Failed to configure fading (%d)", rc);
		return rc;
	}

	/* Update the device configuration registers */
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_UPDATE_CMD, LP581X_UPDATE_CMD);
	if (rc < 0) {
		LOG_DBG("Failed to update configs (%d)", rc);
		return rc;
	}
	/* LED output has glitches if enabled too quickly after being enabled */
	k_sleep(K_MSEC(250));
	return 0;
}

static int lp581x_disable(const struct device *dev)
{
	const struct lp581x_config *config = dev->config;

	/* Write the magic value to the shutdown command register */
	return i2c_reg_write_byte_dt(&config->bus, LP581X_REG_SHUTDOWN_CMD, LP581X_SHUTDOWN_CMD);
}

static int lp581x_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct lp581x_config *config = dev->config;
	uint8_t reg;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		/* Check whether device is available on the I2C bus */
		if (i2c_reg_read_byte_dt(&config->bus, LP581X_REG_CHIP_EN, &reg) < 0) {
			/* Device is already in shutdown mode */
			LOG_DBG("Already shutdown");
			return 0;
		}
		/* Put into shutdown mode */
		return lp581x_disable(dev);
	case PM_DEVICE_ACTION_SUSPEND:
		return lp581x_disable(dev);
	case PM_DEVICE_ACTION_RESUME:
		return lp581x_enable(dev);
	default:
		return -ENOTSUP;
	}
}

static int lp581x_led_init(const struct device *dev)
{
	const struct lp581x_config *config = dev->config;

	if (!device_is_ready(config->bus.bus)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}
	return pm_device_driver_init(dev, lp581x_pm_action);
}

static DEVICE_API(led, lp581x_led_api) = {
	.set_brightness = lp581x_led_set_brightness,
};

#define LP581X_DEFINE(inst, model, _num_leds, auto_anim)                                           \
	BUILD_ASSERT(DT_INST_PROP(inst, out0_current_max) <= LP581X_MAX_CURRENT_SETTING,           \
		     "Channel 0 current must be between 0 and 25.5 mA.");                          \
	BUILD_ASSERT(DT_INST_PROP(inst, out1_current_max) <= LP581X_MAX_CURRENT_SETTING,           \
		     "Channel 1 current must be between 0 and 25.5 mA.");                          \
	BUILD_ASSERT(DT_INST_PROP(inst, out2_current_max) <= LP581X_MAX_CURRENT_SETTING,           \
		     "Channel 2 current must be between 0 and 25.5 mA.");                          \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, out3_current_max, 0) <= LP581X_MAX_CURRENT_SETTING,     \
		     "Channel 3 current must be between 0 and 25.5 mA.");                          \
	static const struct lp581x_config model##_config_##inst = {                                \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.out0_current = DT_INST_PROP(inst, out0_current_max),                              \
		.out1_current = DT_INST_PROP(inst, out1_current_max),                              \
		.out2_current = DT_INST_PROP(inst, out2_current_max),                              \
		.out3_current = DT_INST_PROP_OR(inst, out3_current_max, 0),                        \
		.fade_duration_idx = DT_INST_ENUM_IDX(inst, fade_duration_ms),                     \
		.exponential_fading = DT_INST_PROP(inst, exponential_fading),                      \
		.num_leds = _num_leds,                                                             \
	};                                                                                         \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, lp581x_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, &lp581x_led_init, PM_DEVICE_DT_INST_GET(inst), NULL,           \
			      &model##_config_##inst, POST_KERNEL, CONFIG_LED_INIT_PRIORITY,       \
			      &lp581x_led_api);

#define DT_DRV_COMPAT ti_lp5814
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP581X_DEFINE, lp5814, 4, true)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT ti_lp5815
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP581X_DEFINE, lp5815, 3, true)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT ti_lp5816
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP581X_DEFINE, lp5816, 4, false)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT ti_lp5817
DT_INST_FOREACH_STATUS_OKAY_VARGS(LP581X_DEFINE, lp5817, 3, false)
#undef DT_DRV_COMPAT
