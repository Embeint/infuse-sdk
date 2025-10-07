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

/* Output current limits in 0.1 mA */
#define LP581X_MAX_CURRENT_SETTING 255

enum lp581x_regs {
	LP581X_REG_CHIP_EN = 0x00,
	LP581X_REG_DEV_CONFIG0 = 0x01,
	LP581X_REG_DEV_CONFIG1 = 0x02,
	LP581X_REG_DEV_CONFIG2 = 0x03,
	LP581X_REG_DEV_CONFIG3 = 0x04,
	LP581X_REG_DEV_CONFIG4 = 0x05,
	LP581X_REG_ENGINE_CONFIG0 = 0x06,
	LP581X_REG_ENGINE_CONFIG1 = 0x07,
	LP581X_REG_ENGINE_CONFIG2 = 0x08,
	LP581X_REG_ENGINE_CONFIG3 = 0x09,
	LP581X_REG_ENGINE_CONFIG4 = 0x0A,
	LP581X_REG_ENGINE_CONFIG5 = 0x0B,
	LP581X_REG_ENGINE_CONFIG6 = 0x0C,
	LP581X_REG_SHUTDOWN_CMD = 0x0D,
	LP581X_REG_RESET_CMD = 0x0E,
	LP581X_REG_UPDATE_CMD = 0x0F,
	LP581X_REG_START_CMD = 0x10,
	LP581X_REG_STOP_CMD = 0x11,
	LP581X_REG_PAUSE_CONTINUE = 0x12,
	LP581X_REG_FLAG_CLR = 0x13,
	LP581X_REG_OUT0_DC = 0x14,
	LP581X_REG_OUT1_DC = 0x15,
	LP581X_REG_OUT2_DC = 0x16,
	LP581X_REG_OUT3_DC = 0x16,
	LP581X_REG_OUT0_MANUAL_PWM = 0x18,
	LP581X_REG_OUT1_MANUAL_PWM = 0x19,
	LP581X_REG_OUT2_MANUAL_PWM = 0x1A,
	LP581X_REG_OUT3_MANUAL_PWM = 0x1B,
	LP581X_REG_FLAG = 0x40,
};

enum {
	LP581X_CHIP_EN_CHIP_ENABLE = BIT(0),
	LP581X_CHIP_EN_CHIP_DISABLE = 0x00,
	LP581X_CHIP_EN_INSTABLINK_ENABLE = 0x00,
	LP581X_CHIP_EN_INSTABLINK_DISABLE = BIT(1),
};

enum {
	LP581X_CONFIG1_OUT0_EN = BIT(0),
	LP581X_CONFIG1_OUT1_EN = BIT(1),
	LP581X_CONFIG1_OUT2_EN = BIT(2),
	LP581X_CONFIG1_OUT3_EN = BIT(3),
};

enum {
	LP581X_CONFIG2_OUT0_FADE_EN = BIT(0),
	LP581X_CONFIG2_OUT1_FADE_EN = BIT(1),
	LP581X_CONFIG2_OUT2_FADE_EN = BIT(2),
	LP581X_CONFIG2_OUT3_FADE_EN = BIT(3),
};

enum {
	LP581X_CONFIG3_OUT0_AUTO_EN = BIT(0),
	LP581X_CONFIG3_OUT1_AUTO_EN = BIT(1),
	LP581X_CONFIG3_OUT2_AUTO_EN = BIT(2),
	LP581X_CONFIG3_OUT3_AUTO_EN = BIT(3),
	LP581X_CONFIG3_OUT0_EXP_EN = BIT(4),
	LP581X_CONFIG3_OUT1_EXP_EN = BIT(5),
	LP581X_CONFIG3_OUT2_EXP_EN = BIT(6),
	LP581X_CONFIG3_OUT3_EXP_EN = BIT(7),
};

enum {
	LP581X_FLAG_CLR_POR = BIT(0),
	LP581X_FLAG_CLR_TSD = BIT(1),
};

enum {
	LP581X_FLAG_POR = BIT(0),
	LP581X_FLAG_TSD = BIT(1),
	LP581X_FLAG_ENGINE_BUSY = BIT(2),
	LP581X_FLAG_OUT0_ENGINE_BUSY = BIT(3),
	LP581X_FLAG_OUT1_ENGINE_BUSY = BIT(4),
	LP581X_FLAG_OUT2_ENGINE_BUSY = BIT(5),
	LP581X_FLAG_OUT3_ENGINE_BUSY = BIT(6),
};

#define LP581X_SHUTDOWN_CMD 0x33
#define LP581X_RESET_CMD    0xCC
#define LP581X_UPDATE_CMD   0x55
#define LP581X_START_CMD    0xFF
#define LP581X_STOP_CMD     0xAA
#define LP581X_PAUSE_CMD    0x01
#define LP581X_CONTINUE_CMD 0x00

struct lp581x_config {
	struct i2c_dt_spec bus;
	uint8_t out0_current;
	uint8_t out1_current;
	uint8_t out2_current;
	uint8_t out3_current;
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
