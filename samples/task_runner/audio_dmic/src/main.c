/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/drivers/charger.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_AUDIO_DMIC,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		/* Run for 8 seconds every 10 seconds */
		.periodicity.lockout.lockout_s = TASK_RUNNER_LOCKOUT_IGNORE_FIRST | 10,
		.timeout_s = 8,
		.task_args.infuse.audio_dmic =
			{
				.sample_rate_hz = 16000,
				.gain_db = 20,
			},
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_BT_PERIPHERAL,
					.tdf_mask = TASK_AUDIO_DMIC_LOG_METADATA |
						    TASK_AUDIO_DMIC_LOG_SAMPLES,
				},
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
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
			},
	},
#endif /* CONFIG_BT */
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (AUDIO_DMIC_TASK, DT_ALIAS(dmic_dev)));

int main(void)
{
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
