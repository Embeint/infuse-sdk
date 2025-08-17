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

#include <infuse/algorithm_runner/runner.h>
#include <infuse/algorithm_runner/algorithms/tilt.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_IMU,
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 50,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL,
					.tdf_mask = TASK_IMU_LOG_ACC | TASK_IMU_LOG_GYR,
				},
			},
		.task_args.infuse.imu =
			{
				.accelerometer =
					{
						.range_g = 2,
						.rate_hz = 30,
					},
				.gyroscope =
					{
						.range_dps = 500,
						.rate_hz = 15,
					},
				.fifo_sample_buffer = 100,
			},
	},
#ifdef CONFIG_BT
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 250,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_ACCEL,
			},
	},
#endif /* CONFIG_BT */
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (IMU_TASK, DEVICE_DT_GET(DT_ALIAS(imu0))));

ALGORITHM_TILT_DEFINE(alg_tilt, TDF_DATA_LOGGER_SERIAL, ALGORITHM_TILT_LOG_ANGLE, 0.025f, 5);

int main(void)
{
	KV_KEY_TYPE(KV_KEY_GRAVITY_REFERENCE) gravity_default = {0, 0, -8192};

	/* Start the watchdog */
	(void)infuse_watchdog_start();

#ifdef CONFIG_NETWORKING
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);
#endif /* CONFIG_NETWORKING */

	/* Set a default gravity reference for the sample (-z axis) */
	if (!kv_store_key_exists(KV_KEY_GRAVITY_REFERENCE)) {
		(void)KV_STORE_WRITE(KV_KEY_GRAVITY_REFERENCE, &gravity_default);
	}

	/* Start the algorithm runner */
	algorithm_runner_init();
	algorithm_runner_register(&alg_tilt);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
