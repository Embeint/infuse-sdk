/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/drivers/watchdog.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

GATEWAY_HANDLER_DEFINE(serial_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_serial)));

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 10,
		.task_args.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY,
			},
	},
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL));

int main(void)
{
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers */
	epacket_set_receive_handler(epacket_serial, serial_backhaul_handler);

	/* Always listening on serial */
	epacket_receive(epacket_serial, K_FOREVER);

	/* Send key identifiers on boot */
	epacket_send_key_ids(epacket_serial, K_FOREVER);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(led0))
	/* The LED controller may be omitted from a peripheral-free build. */
	const struct gpio_dt_spec led = {
		.port = device_get_binding(DEVICE_DT_NAME(DT_GPIO_CTLR(DT_ALIAS(led0), gpios))),
		.pin = DT_GPIO_PIN(DT_ALIAS(led0), gpios),
		.dt_flags = DT_GPIO_FLAGS(DT_ALIAS(led0), gpios),
	};
	int rc = gpio_is_ready_dt(&led) ? gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)
					: -ENODEV;

	/* Blink LED once a second as proof of life when it is available. */
	while (rc == 0) {
		rc = gpio_pin_set_dt(&led, 1);
		if (rc < 0) {
			break;
		}
		k_sleep(K_MSEC(10));
		rc = gpio_pin_set_dt(&led, 0);
		k_sleep(K_MSEC(990));
	}
	LOG_WRN("Optional LED unavailable (%d)", rc);
	k_sleep(K_FOREVER);
#else
	/* No more work to do in this context */
	k_sleep(K_FOREVER);
#endif
}
