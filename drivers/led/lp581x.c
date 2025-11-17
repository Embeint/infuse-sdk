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

#include <infuse/drivers/led/lp581x.h>

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
	bool animation_support;
};

struct lp581x_data {
	bool was_animating;
};

LOG_MODULE_REGISTER(lp5815, LOG_LEVEL_DBG);

/** Single transaction for compatibility, instead of double transaction of `i2c_burst_write` */
static int lp581x_reg_multi_write(const struct device *dev, uint8_t start_addr, const uint8_t *buf,
				  uint32_t num_bytes)
{
	const struct lp581x_config *config = dev->config;
	uint8_t write_buffer[1 + sizeof(struct lp581x_pattern_regs)];

	if (num_bytes > sizeof(struct lp581x_pattern_regs)) {
		return -EINVAL;
	}
	write_buffer[0] = start_addr;
	memcpy(write_buffer + 1, buf, num_bytes);

	return i2c_write_dt(&config->bus, write_buffer, 1 + num_bytes);
}

static int lp581x_animation_engine_busy(const struct device *dev)
{
	const struct lp581x_config *config = dev->config;
	uint8_t flag;
	int rc;

	rc = i2c_reg_read_byte_dt(&config->bus, LP581X_REG_FLAG, &flag);
	if (rc < 0) {
		LOG_DBG("Failed to read FLAG register (%d)", rc);
		return rc;
	}
	return flag & LP581X_FLAG_ENGINE_BUSY ? 1 : 0;
}

BUILD_ASSERT(sizeof(struct lp581x_pattern_regs) == 9);
BUILD_ASSERT(LP581X_REG_PATTERN1_BASE ==
	     LP581X_REG_PATTERN0_BASE + sizeof(struct lp581x_pattern_regs));
BUILD_ASSERT(LP581X_REG_PATTERN2_BASE ==
	     LP581X_REG_PATTERN1_BASE + sizeof(struct lp581x_pattern_regs));
BUILD_ASSERT(LP581X_REG_PATTERN3_BASE ==
	     LP581X_REG_PATTERN2_BASE + sizeof(struct lp581x_pattern_regs));

int lp581x_animation_pattern_program(const struct device *dev, uint8_t pattern_idx,
				     const struct lp581x_animation_pattern *pattern)
{
	const struct lp581x_config *config = dev->config;
	int rc;

	/* Input validation */
	if (config->animation_support == false) {
		return -ENOTSUP;
	}
	if (pattern_idx >= LP581X_NUM_PATTERNS) {
		return -EINVAL;
	}
	if (pattern->sloper.play_count > LP581X_PATTERN_PLAY_FOREVER) {
		return -EINVAL;
	}
	if (pattern->pre_pause.duration > _LP581X_PHASE_END) {
		return -EINVAL;
	}
	if (pattern->pre_pause.duration > _LP581X_PHASE_END) {
		return -EINVAL;
	}
	for (int i = 0; i < ARRAY_SIZE(pattern->sloper.duration); i++) {
		if (pattern->sloper.duration[i] >= _LP581X_PHASE_END) {
			return -EINVAL;
		}
	}

	/* Check if animation engines are already running */
	rc = lp581x_animation_engine_busy(dev);
	if (rc != 0) {
		return rc == 1 ? -EBUSY : rc;
	}

	/* Construct the pattern */
	struct lp581x_pattern_regs reg_vals = {
		.pause_time = (pattern->pre_pause.duration << 4) | pattern->post_pause.duration,
		.play_count = pattern->sloper.play_count,
		.pwm =
			{
				pattern->pre_pause.duration,
				pattern->sloper.pwm[0],
				pattern->sloper.pwm[1],
				pattern->sloper.pwm[2],
				pattern->post_pause.duration,
			},
		.sloper1 = (pattern->sloper.duration[1] << 4) | pattern->sloper.duration[0],
		.sloper2 = (pattern->sloper.duration[3] << 4) | pattern->sloper.duration[2],
	};
	uint8_t reg = LP581X_REG_PATTERN0_BASE + (pattern_idx * sizeof(struct lp581x_pattern_regs));

	return lp581x_reg_multi_write(dev, reg, (void *)&reg_vals, sizeof(reg_vals));
}

static int engine_configure_order(const struct device *dev, uint8_t engine_idx,
				  const struct lp581x_animation_engine_config *cfg,
				  uint8_t order_enables[2])
{
	const struct lp581x_config *config = dev->config;

	/* Store enabled channels */
	uint8_t order_en = ((cfg->order[0] == LP581X_PATTERN_SKIP) ? 0 : BIT(0)) |
			   ((cfg->order[1] == LP581X_PATTERN_SKIP) ? 0 : BIT(1)) |
			   ((cfg->order[2] == LP581X_PATTERN_SKIP) ? 0 : BIT(2)) |
			   ((cfg->order[3] == LP581X_PATTERN_SKIP) ? 0 : BIT(3));

	order_enables[engine_idx / 2] |= (order_en << (engine_idx % 2 ? 4 : 0));

	/* Engine pattern order */
	uint8_t order = ((cfg->order[0] == LP581X_PATTERN_SKIP) ? 0 : cfg->order[0] << 0) |
			((cfg->order[1] == LP581X_PATTERN_SKIP) ? 0 : cfg->order[1] << 2) |
			((cfg->order[2] == LP581X_PATTERN_SKIP) ? 0 : cfg->order[2] << 4) |
			((cfg->order[3] == LP581X_PATTERN_SKIP) ? 0 : cfg->order[3] << 6);

	return i2c_reg_write_byte_dt(&config->bus, LP581X_REG_ENGINE_CONFIG0 + engine_idx, order);
}

int lp581x_animation_engines_configure(const struct device *dev,
				       const struct lp581x_animation_engines_config *engines_config)
{
	const struct lp581x_config *config = dev->config;
	int rc;

	/* Input validation */
	if (config->animation_support == false) {
		return -ENOTSUP;
	}
	if (engines_config->num_engines > LP581X_NUM_ENGINES) {
		return -EINVAL;
	}
	for (int i = 0; i < engines_config->num_engines; i++) {
		const struct lp581x_animation_engine_config *cfg = &engines_config->engines[i];

		for (int j = 0; j < ARRAY_SIZE(cfg->order); j++) {
			if (cfg->order[j] > LP581X_PATTERN_SKIP) {
				return -EINVAL;
			}
		}
	}
	for (int i = 0; i < ARRAY_SIZE(engines_config->led_channel_engines); i++) {
		if (engines_config->led_channel_engines[i] >= LP581X_NUM_ENGINES) {
			return -EINVAL;
		}
	}

	/* Check if animation engines are already running */
	rc = lp581x_animation_engine_busy(dev);
	if (rc != 0) {
		return rc == 1 ? -EBUSY : rc;
	}

	/* Configure engine orders */
	uint8_t order_enables[2] = {0};

	for (int i = 0; i < engines_config->num_engines; i++) {
		const struct lp581x_animation_engine_config *cfg = &engines_config->engines[i];

		rc = engine_configure_order(dev, i, cfg, order_enables);
		if (rc < 0) {
			LOG_DBG("Failed to configure engine %d (%d)", i, rc);
			return rc;
		}
	}
	rc = lp581x_reg_multi_write(dev, LP581X_REG_ENGINE_CONFIG4, order_enables,
				    sizeof(order_enables));
	if (rc < 0) {
		LOG_DBG("Failed to write order enables (%d)", rc);
		return rc;
	}

	/* Engine channel output */
	uint8_t output_channels = (engines_config->led_channel_engines[0] << 0) |
				  (engines_config->led_channel_engines[1] << 2) |
				  (engines_config->led_channel_engines[2] << 4) |
				  (engines_config->led_channel_engines[3] << 6);

	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG4, output_channels);
	if (rc < 0) {
		LOG_DBG("Failed to write output channels (%d)", rc);
		return rc;
	}

	/* Engine repeats */
	uint8_t engine_repeats = (engines_config->engines[0].repeats << 0) |
				 (engines_config->engines[1].repeats << 2) |
				 (engines_config->engines[2].repeats << 4) |
				 (engines_config->engines[3].repeats << 6);

	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_ENGINE_CONFIG6, engine_repeats);
	if (rc < 0) {
		LOG_DBG("Failed to write engine repeats (%d)", rc);
		return rc;
	}

	return 0;
}

int lp581x_animation_start(const struct device *dev, uint8_t led_bitmask)
{
	const struct lp581x_config *config = dev->config;
	struct lp581x_data *data = dev->data;
	int rc;

	if (config->animation_support == false) {
		return -ENOTSUP;
	}
	if (led_bitmask > BIT_MASK(config->num_leds)) {
		return -EINVAL;
	}

	/* Check if animation engines are already running */
	rc = lp581x_animation_engine_busy(dev);
	if (rc != 0) {
		return rc == 1 ? -EBUSY : rc;
	}

	/* Notify on/off logic to handle */
	data->was_animating = true;

	/* Write the desired channels (and exponential fading config) */
	if (config->exponential_fading) {
		led_bitmask |= ((config->num_leds == 4) ? 0xF0 : 0x70);
	}
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG3, led_bitmask);
	if (rc < 0) {
		LOG_DBG("Failed to configure fading (%d)", rc);
		return rc;
	}

	/* Update all configuration registers */
	rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_UPDATE_CMD, LP581X_UPDATE_CMD);
	if (rc < 0) {
		LOG_DBG("Failed to update registers (%d)", rc);
		return rc;
	}

	/* Write the start command */
	return i2c_reg_write_byte_dt(&config->bus, LP581X_REG_START_CMD, LP581X_START_CMD);
}

int lp581x_animation_stop(const struct device *dev)
{
	const struct lp581x_config *config = dev->config;
	int rc;

	if (config->animation_support == false) {
		return -ENOTSUP;
	}

	/* Require that animation engines are already running */
	rc = lp581x_animation_engine_busy(dev);
	if (rc != 1) {
		return rc == 1 ? -EAGAIN : rc;
	}

	/* Write the stop command */
	return i2c_reg_write_byte_dt(&config->bus, LP581X_REG_STOP_CMD, LP581X_STOP_CMD);
}

static int lp581x_led_set_brightness(const struct device *dev, uint32_t led, uint8_t value)
{
	const struct lp581x_config *config = dev->config;
	struct lp581x_data *data = dev->data;
	uint8_t reg;
	int rc;

	if (led >= config->num_leds) {
		return -EINVAL;
	}

	if (data->was_animating) {
		/* Clear animation bits (preserve fading config) */
		if (config->exponential_fading) {
			reg = (config->num_leds == 4) ? 0xF0 : 0x70;
		} else {
			reg = 0x00;
		}
		rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_DEV_CONFIG3, reg);
		if (rc < 0) {
			return rc;
		}
		/* Update the device configuration registers */
		rc = i2c_reg_write_byte_dt(&config->bus, LP581X_REG_UPDATE_CMD, LP581X_UPDATE_CMD);
		if (rc < 0) {
			return rc;
		}
		data->was_animating = false;
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
		.animation_support = auto_anim,                                                    \
	};                                                                                         \
	static struct lp581x_data model##_data_##inst;                                             \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, lp581x_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, &lp581x_led_init, PM_DEVICE_DT_INST_GET(inst),                 \
			      &model##_data_##inst, &model##_config_##inst, POST_KERNEL,           \
			      CONFIG_LED_INIT_PRIORITY, &lp581x_led_api);

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
