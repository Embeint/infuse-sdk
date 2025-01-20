/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/time/epoch.h>
#include <infuse/tdf/definitions.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/watchdog.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 1 * SEC_PER_MIN,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 2,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
};
struct task_schedule_state states[ARRAY_SIZE(schedules)];

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0))));

int main(void)
{
	/* Start watchdog */
	infuse_watchdog_start();

	k_sleep(K_SECONDS(2));

	/* Turn on the interface */
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
