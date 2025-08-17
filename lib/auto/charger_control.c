/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_gpio_charger_control

#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

static const struct {
	struct gpio_dt_spec control;
	int32_t temperature_max;
	int32_t temperature_min;
	uint16_t hysteresis;
} charger_control_config = {
	.control = GPIO_DT_SPEC_INST_GET_BY_IDX(0, control_gpios, 0),
	.temperature_max = DT_INST_PROP(0, temperature_max) - 273,
	.temperature_min = DT_INST_PROP(0, temperature_min) - 273,
	.hysteresis = DT_INST_PROP(0, control_hysteresis),
};
static struct {
	bool enabled;
	uint8_t loggers;
} charger_control_data;

static void new_env_data(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
ZBUS_LISTENER_DEFINE(charger_env_listener, new_env_data);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_AMBIENT_ENV), charger_env_listener, 5);

LOG_MODULE_REGISTER(charge_control, CONFIG_CHARGER_CONTROL_LOG_LEVEL);

static void new_env_data(const struct zbus_channel *chan)
{
	const struct tdf_ambient_temp_pres_hum *env = zbus_chan_const_msg(chan);
	int32_t current = env->temperature / 1000;
	bool en = charger_control_data.enabled;
	struct tdf_charger_en_control control;
	bool change = false;
	int min;
	int max;

	/* Determine thresholds and whether control needs to change */
	if (en) {
		min = charger_control_config.temperature_min;
		max = charger_control_config.temperature_max;

		if (!IN_RANGE(current, min, max)) {
			change = true;
		}
	} else {
		min = charger_control_config.temperature_min + charger_control_config.hysteresis;
		max = charger_control_config.temperature_max - charger_control_config.hysteresis;

		if (IN_RANGE(current, min, max)) {
			change = true;
		}
	}

	if (!change) {
		return;
	}

	LOG_WRN("%s charger, %d %s [%d, %d]", en ? "Disabling" : "Enabling", current,
		en ? "outside" : "within", min, max);

	/* Set control line to new state */
	gpio_pin_set_dt(&charger_control_config.control, en ? 0 : 1);

	/* Update internal state */
	charger_control_data.enabled = !charger_control_data.enabled;

	/* Log control change as a TDF */
	control.enabled = charger_control_data.enabled ? 1 : 0;
	TDF_DATA_LOGGER_LOG(charger_control_data.loggers, TDF_CHARGER_EN_CONTROL, epoch_time_now(),
			    &control);
}

void auto_charger_control_log_configure(uint8_t tdf_logger_mask)
{
	charger_control_data.loggers = tdf_logger_mask;
}

int charge_control_init(void)
{
	/* Charger enabled by default */
	gpio_pin_configure_dt(&charger_control_config.control, GPIO_OUTPUT_ACTIVE);
	charger_control_data.enabled = true;
	charger_control_data.loggers = 0;
	return 0;
}

SYS_INIT(charge_control_init, POST_KERNEL, 0);
