/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>

#include <infuse/validation/core.h>
#include <infuse/validation/button.h>

#define TEST "BTN"

struct button_gpio_cfg {
	const struct gpio_dt_spec *button;
	struct gpio_callback cb;
	uint8_t events_observed;
	uint8_t event_required;
};

static K_SEM_DEFINE(button_complete, 0, 1);

static void button_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct button_gpio_cfg *button_cfg = CONTAINER_OF(cb, struct button_gpio_cfg, cb);

	if (gpio_pin_get_dt(button_cfg->button)) {
		button_cfg->events_observed |= VALIDATION_BUTTON_MODE_TRIGGER;
		VALIDATION_REPORT_INFO(TEST, "Trigger transition observed");
	} else {
		button_cfg->events_observed |= VALIDATION_BUTTON_MODE_RELEASE;
		VALIDATION_REPORT_INFO(TEST, "Release transition observed");
	}

	if ((button_cfg->events_observed & button_cfg->event_required) ==
	    button_cfg->event_required) {
		k_sem_give(&button_complete);
	}
}

int infuse_validation_button(const struct gpio_dt_spec *button, uint8_t flags)
{
	const struct device *dev = button->port;
	struct button_gpio_cfg button_cfg = {
		.button = button, .event_required = flags, .events_observed = 0};
	int rc = 0;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s:%d", dev->name, button->pin);

	/* Check hardware initialisation */
	if (!gpio_is_ready_dt(button)) {
		VALIDATION_REPORT_ERROR(TEST, "Device is not ready");
		return -ENODEV;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		return rc;
	}

	/* Setup gpio pin */
	rc = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to configure pin as input (%d)", rc);
		goto handle_end_pm;
	}

	/* Configure interrupt */
	rc = gpio_pin_interrupt_configure_dt(button, GPIO_INT_EDGE_BOTH);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to configure interrupt (%d)", rc);
		goto handle_end_gpio;
	}

	/* Register callback */
	gpio_init_callback(&button_cfg.cb, button_cb, BIT(button->pin));
	rc = gpio_add_callback(button->port, &button_cfg.cb);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to register interrupt callback (%d)", rc);
		goto handle_end_interrupt;
	}

	rc = k_sem_take(&button_complete,
			K_SECONDS(CONFIG_INFUSE_VALIDATION_BUTTON_ACTION_TIMEOUT));
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST,
					"Button events (0x%x) did not occur within timeout (%ds)",
					button_cfg.event_required ^ button_cfg.events_observed,
					CONFIG_INFUSE_VALIDATION_BUTTON_ACTION_TIMEOUT);
	}

handle_end_interrupt:
	if (gpio_remove_callback(button->port, &button_cfg.cb)) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to unregister interrupt");
		}
	}
	if (gpio_pin_interrupt_configure_dt(button, GPIO_INT_DISABLE) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to disable interrupt");
		}
	}
handle_end_gpio:
	gpio_pin_configure_dt(button, GPIO_DISCONNECTED);
handle_end_pm:
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}

	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "PASSED");
	}
	return rc;
}
