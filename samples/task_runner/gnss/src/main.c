/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/drivers/gnss.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/auto/time_sync_log.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_GNSS,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.timeout_s = 115,
		.periodicity.fixed.period_s = 10,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL,
					.tdf_mask = TASK_GNSS_LOG_PVT,
				},
			},
		.task_args.infuse.gnss =
			{
				.constellations =
					GNSS_SYSTEM_GPS | GNSS_SYSTEM_QZSS | GNSS_SYSTEM_SBAS,
				.flags = TASK_GNSS_FLAGS_PERFORMANCE_MODE |
					 TASK_GNSS_FLAGS_RUN_FOREVER,
				.accuracy_m = 5,
				.position_dop = 40,
			},
	},
#if DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0))
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 2,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL,
					.tdf_mask = TASK_BATTERY_LOG_COMPLETE,
				},
			},
	},
#endif /* DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0)) */
#ifdef CONFIG_BT
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 200,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE |
					TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_BATTERY,
			},
	},
#endif /* CONFIG_BT */
};

#if DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0))
#define BAT_TASK_DEFINE (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0)))
#else
#define BAT_TASK_DEFINE
#endif /* DT_NODE_EXISTS(DT_ALIAS(fuel_gauge0)) */

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL), BAT_TASK_DEFINE,
			 (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(gnss))));

int main(void)
{
	/* Log time sync changes */
	auto_time_sync_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_TIME_SYNC_LOG_SYNCS);

	/* Start the watchdog */
	(void)infuse_watchdog_start();

#ifdef CONFIG_NETWORKING
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);
#endif /* CONFIG_NETWORKING */

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
