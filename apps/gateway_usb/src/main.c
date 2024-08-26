/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
		.periodicity.lockout.lockout_s = SEC_PER_MIN,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY,
			},
	},
};

struct task_schedule_state states[ARRAY_SIZE(schedules)];

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK));

int main(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers */
	epacket_set_receive_handler(epacket_serial, serial_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_adv, serial_backhaul_handler);

	/* Always listening on Bluetooth and serial */
	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
