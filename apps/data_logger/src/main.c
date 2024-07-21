/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_UDP |
					   TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV,
			},
	},
	{
		.task_id = TASK_ID_IMU,
		.validity = TASK_VALID_ALWAYS,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP,
					.tdf_mask = TASK_IMU_LOG_ACC | TASK_IMU_LOG_GYR,
				},
			},
		.task_args.infuse.imu =
			{
				.accelerometer =
					{
						.range_g = 4,
						.rate_hz = 50,
					},
				.gyroscope =
					{
						.range_dps = 500,
						.rate_hz = 50,
					},
				.fifo_sample_buffer = 100,
			},
	},
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_FLASH,
					.tdf_mask = TASK_BATTERY_LOG_COMPLETE,
				},
			},
	},
	{
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_FLASH,
					.tdf_mask = TASK_ENVIRONMENTAL_LOG_TPH,
				},
			},
	},
};
struct task_schedule_state states[ARRAY_SIZE(schedules)];

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK),
			 (IMU_TASK, DEVICE_DT_GET(DT_NODELABEL(bmi270))),
			 (BATTERY_TASK, DEVICE_DT_GET(DT_NODELABEL(vbatt))),
			 (ENVIRONMENTAL_TASK, DEVICE_DT_GET(DT_NODELABEL(bme688))));

int main(void)
{
	/* Start the watchdog */
	(void)infuse_watchdog_start();

#ifdef CONFIG_NETWORKING
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);
#endif /* CONFIG_NETWORKING */

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
