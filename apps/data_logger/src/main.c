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

#include <infuse/time/civil.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>
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
				.logger = TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_UDP |
					  TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
			},
	},
};
struct task_schedule_state states[ARRAY_SIZE(schedules)];

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, TDF_LOGGER_TASK);

int main(void)
{
	int iterate = k_uptime_seconds() + 1;

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Iterate task runner forever */
	while (true) {
		task_runner_iterate(k_uptime_seconds(), civil_time_seconds(civil_time_now()), 100);

		k_sleep(K_TIMEOUT_ABS_MS(iterate * MSEC_PER_SEC));
		iterate += 1;
	}
}
