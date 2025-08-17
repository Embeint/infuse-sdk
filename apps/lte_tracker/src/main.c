/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
#include <infuse/epacket/packet.h>
#include <infuse/bluetooth/legacy_adv.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_NETWORK_SCAN,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 5 * SEC_PER_MIN,
		.task_args.infuse.network_scan =
			{
#ifdef CONFIG_WIFI
				.flags = TASK_NETWORK_SCAN_FLAGS_LTE_CELLS |
					 TASK_NETWORK_SCAN_FLAGS_WIFI_CELLS |
					 TASK_NETWORK_SCAN_FLAGS_SKIP_LTE_IF_WIFI_GOOD,
				.wifi =
					{
						.flags =
							TASK_NETWORK_SCAN_WIFI_FLAGS_SCAN_PROGRESSIVE,
						.desired_aps = 4,
						.max_aps = 8,
					},
				.lte =
					{
						.desired_cells = 4,
					},
#else
				.flags = TASK_NETWORK_SCAN_FLAGS_LTE_CELLS,
				.lte =
					{
						.desired_cells = 4,
					},
#endif
			},
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP,
					.tdf_mask = TASK_NETWORK_SCAN_LOG_WIFI_AP |
						    TASK_NETWORK_SCAN_LOG_LTE_CELLS,
				},
			},
	},
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		/* Push UDP packet after network scan completes */
		.periodicity_type = TASK_PERIODICITY_AFTER,
		.periodicity.after.schedule_idx = 0,
		.periodicity.after.duration_s = 0,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_NET_CONN |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV,
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
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
	},
#if DT_NODE_EXISTS(DT_ALIAS(environmental0))
	{
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5,
	},
#endif /* DT_NODE_EXISTS(DT_ALIAS(environmental0)) */
#ifdef CONFIG_BT
	{
		.task_id = TASK_ID_TDF_LOGGER_ALT1,
		.validity = TASK_VALID_PERMANENTLY_RUNS,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 200,
				.per_run = 3,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV |
					TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
#endif /* CONFIG_BT */
};

#if DT_NODE_EXISTS(DT_ALIAS(environmental0))
#define ENV_TASK_DEFINE (ENVIRONMENTAL_TASK, DEVICE_DT_GET(DT_ALIAS(environmental0)))
#else
#define ENV_TASK_DEFINE
#endif
#ifdef CONFIG_BT
#define BT_TASK_DEFINE (TDF_LOGGER_ALT1_TASK, NULL)
#else
#define BT_TASK_DEFINE
#endif

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL), BT_TASK_DEFINE,
			 ENV_TASK_DEFINE, (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0))),
			 (NETWORK_SCAN_TASK, NULL));

int main(void)
{
	/* Constant ePacket flags */
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_SELF);

	/* Start watchdog */
	infuse_watchdog_start();

	k_sleep(K_SECONDS(2));

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

#ifdef CONFIG_BT
	/* Start legacy Bluetooth advertising to workaround iOS and
	 * Nordic Softdevice connection issues.
	 */
	bluetooth_legacy_advertising_run();
#endif /* CONFIG_BT */

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* No more work to do in this context */
	k_sleep(K_FOREVER);
}
