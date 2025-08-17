/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT zephyr_spdt_switch

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/logging/log.h>

#include <infuse/shared/device.h>

LOG_MODULE_REGISTER(spdt_switch, CONFIG_SHARED_DEVICE_LOG_LEVEL);

struct spdt_switch_config {
	struct gpio_dt_spec control;
};

struct spdt_switch_data {
	int active_priority;
	int inactive_priority;
};

static int spdt_switch_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct spdt_switch_config *config = dev->config;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_TURN_ON:
		/* When not actively controlled, float to default state */
		gpio_pin_configure_dt(&config->control, GPIO_DISCONNECTED);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		/* When not powered, float with no configuration */
		LOG_DBG("Switch control now disconnected");
		gpio_pin_configure(config->control.port, config->control.pin, GPIO_DISCONNECTED);
		break;
	default:
		rc = -ENOTSUP;
	}

	return rc;
}

static void switch_state_update(const struct device *dev)
{
	const struct spdt_switch_config *config = dev->config;
	struct spdt_switch_data *data = dev->data;

	/* No pending requests, revert to floating */
	if ((data->inactive_priority == -1) && (data->active_priority == -1)) {
		LOG_DBG("Switch control now in default state");
		gpio_pin_configure_dt(&config->control, GPIO_DISCONNECTED);
		return;
	}

	/* Set control line to highest priority request */
	if (data->active_priority > data->inactive_priority) {
		LOG_DBG("Switch control now driven active");
		gpio_pin_configure_dt(&config->control, GPIO_OUTPUT_ACTIVE);
	} else {
		LOG_DBG("Switch control now driven inactive");
		gpio_pin_configure_dt(&config->control, GPIO_OUTPUT_INACTIVE);
	}
}

static int spdt_switch_request(const struct device *dev, uint8_t state_priority, uint8_t state)
{
	struct spdt_switch_data *data = dev->data;

	switch (state) {
	case 0:
		if (data->inactive_priority != -1) {
			return -EALREADY;
		}
		data->inactive_priority = state_priority;
		break;
	case 1:
		if (data->active_priority != -1) {
			return -EALREADY;
		}
		data->active_priority = state_priority;
		break;
	default:
		/* Only 0 and 1 states are supported */
		return -EINVAL;
	}

	/* Someone has added a new request on the switch, power up */
	if (pm_device_runtime_get(dev) < 0) {
		return -EIO;
	}

	/* Update state of control line */
	switch_state_update(dev);
	return 0;
}

static int spdt_switch_release(const struct device *dev, uint8_t state_priority)
{
	struct spdt_switch_data *data = dev->data;

	if (state_priority == data->active_priority) {
		data->active_priority = -1;
	} else if (state_priority == data->inactive_priority) {
		data->inactive_priority = -1;
	} else {
		return -EINVAL;
	}

	/* Update state of control line */
	switch_state_update(dev);

	/* Someone has released a request on the switch */
	pm_device_runtime_put(dev);

	return 0;
}

static int spdt_switch_init(const struct device *dev)
{
	const struct spdt_switch_config *cfg = dev->config;
	struct spdt_switch_data *data = dev->data;

	if (!gpio_is_ready_dt(&cfg->control)) {
		LOG_ERR("GPIO port %s is not ready", cfg->control.port->name);
		return -ENODEV;
	}

	/* Default priorities */
	data->active_priority = -1;
	data->inactive_priority = -1;

	return pm_device_driver_init(dev, spdt_switch_pm_action);
}

struct shared_device_api spdt_switch_api = {
	.request = spdt_switch_request,
	.release = spdt_switch_release,
};

#define SPDT_SWITCH_DEVICE(id)                                                                     \
	static const struct spdt_switch_config spdt_switch_##id##_cfg = {                          \
		.control = GPIO_DT_SPEC_INST_GET(id, ctrl_gpios),                                  \
	};                                                                                         \
	static struct spdt_switch_data spdt_switch_##id##_data;                                    \
	PM_DEVICE_DT_INST_DEFINE(id, spdt_switch_pm_action);                                       \
	DEVICE_DT_INST_DEFINE(id, spdt_switch_init, PM_DEVICE_DT_INST_GET(id),                     \
			      &spdt_switch_##id##_data, &spdt_switch_##id##_cfg, POST_KERNEL,      \
			      CONFIG_SHARED_DEVICE_INIT_PRIORITY, &spdt_switch_api);

DT_INST_FOREACH_STATUS_OKAY(SPDT_SWITCH_DEVICE)
