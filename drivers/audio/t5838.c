/*
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT invensense_t5838

#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

struct t5838_config {
	const struct device *dmic;
	struct gpio_dt_spec en_gpio;
	struct gpio_dt_spec thsel_gpio;
	struct gpio_dt_spec wake_gpio;
	struct gpio_dt_spec pdmclk_gpio;
};

struct t5838_data {
};

LOG_MODULE_REGISTER(t5838, CONFIG_T5838_LOG_LEVEL);

static int dmic_nrfx_pdm_configure(const struct device *dev, struct dmic_cfg *cfg)
{
	const struct t5838_config *config = dev->config;

	return dmic_configure(config->dmic, cfg);
}

static int dmic_nrfx_pdm_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	const struct t5838_config *config = dev->config;

	return dmic_trigger(config->dmic, cmd);
}

static int dmic_nrfx_pdm_read(const struct device *dev, uint8_t stream, void **buffer, size_t *size,
			      int32_t timeout)
{
	const struct t5838_config *config = dev->config;

	return dmic_read(config->dmic, stream, buffer, size, timeout);
}

static int t5838_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct t5838_config *config = dev->config;
	int rc;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		if (config->en_gpio.port) {
			gpio_pin_set_dt(&config->en_gpio, 0);
		}
		rc = 0;
		break;
	case PM_DEVICE_ACTION_RESUME:
		if (config->en_gpio.port) {
			gpio_pin_set_dt(&config->en_gpio, 1);
		}
		rc = 0;
		break;
	default:
		rc = -ENOTSUP;
	}
	return rc;
}

static int t5838_init(const struct device *dev)
{
	const struct t5838_config *config = dev->config;

	if (!device_is_ready(config->dmic)) {
		LOG_DBG("Underlying interface not ready");
		return -ENODEV;
	}

	if (config->en_gpio.port) {
		gpio_pin_configure_dt(&config->en_gpio, GPIO_OUTPUT_INACTIVE);
	}

	/* Drive to GND by default */
	gpio_pin_configure_dt(&config->thsel_gpio, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&config->wake_gpio, GPIO_OUTPUT_INACTIVE);
	return pm_device_driver_init(dev, t5838_pm_control);
}

static const struct _dmic_ops dmic_ops = {
	.configure = dmic_nrfx_pdm_configure,
	.trigger = dmic_nrfx_pdm_trigger,
	.read = dmic_nrfx_pdm_read,
};

#define T5838_INIT(inst)                                                                           \
	static const struct t5838_config t5838_##inst##_config = {                                 \
		.dmic = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                       \
		.en_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, en_gpios, {}),                           \
		.thsel_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, thsel_gpios, {}),                     \
		.wake_gpio = GPIO_DT_SPEC_INST_GET(inst, wake_gpios),                              \
		.pdmclk_gpio = GPIO_DT_SPEC_INST_GET(inst, pdmclk_gpios),                          \
	};                                                                                         \
	static struct t5838_data t5838_##inst##_data;                                              \
	PM_DEVICE_DT_INST_DEFINE(inst, t5838_pm_control);                                          \
	DEVICE_DT_INST_DEFINE(inst, t5838_init, PM_DEVICE_DT_INST_GET(inst), &t5838_##inst##_data, \
			      &t5838_##inst##_config, POST_KERNEL, 99, &dmic_ops);

DT_INST_FOREACH_STATUS_OKAY(T5838_INIT)
