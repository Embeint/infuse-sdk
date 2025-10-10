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

#include <infuse/version.h>
#include <infuse/auto/time_sync_log.h>
#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/dfu/exfat.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/lib/memfault.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void custom_tdf_logger(uint8_t tdf_loggers, uint64_t timestamp);

/* Log data to SD card if available, otherwise serial and UDP comms */
#if defined(CONFIG_DATA_LOGGER_EXFAT)
/* External SD card is the preferred logging backend */
#define STORAGE_LOGGER TDF_DATA_LOGGER_REMOVABLE
#elif defined(CONFIG_NRF_MODEM_LIB)
/* If UDP is implemented by LTE, don't use as it will chew up data quotas */
#define STORAGE_LOGGER TDF_DATA_LOGGER_SERIAL
#else
/* Otherwise use serial and UDP (WiFi)*/
#define STORAGE_LOGGER TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP
#endif /* CONFIG_DATA_LOGGER_EXFAT */

static const struct task_schedule schedules[] = {
#ifdef CONFIG_EPACKET_INTERFACE_UDP
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 5 * SEC_PER_MIN,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV |
					TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_ACCEL |
					TASK_TDF_LOGGER_LOG_NET_CONN | TASK_TDF_LOGGER_LOG_CUSTOM,
			},
	},
#endif /* CONFIG_EPACKET_INTERFACE_UDP */
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 2,
		.task_args.infuse.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_SERIAL,
				.random_delay_ms = 1000,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_BATTERY |
					TASK_TDF_LOGGER_LOG_AMBIENT_ENV |
					TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_ACCEL |
					TASK_TDF_LOGGER_LOG_NET_CONN,
			},
	},
	{
		.task_id = TASK_ID_IMU,
		.validity = TASK_VALID_ALWAYS,
		.task_logging =
			{
				{
					.loggers = STORAGE_LOGGER,
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
#if DT_NODE_EXISTS(DT_ALIAS(gnss))
	{
		.task_id = TASK_ID_GNSS,
		.validity = TASK_VALID_ALWAYS,
		.battery_start.lower = 30,
		.battery_terminate.lower = 20,
		.task_logging =
			{
				{
					.loggers = STORAGE_LOGGER,
					.tdf_mask = TASK_GNSS_LOG_PVT,
				},
			},
		.task_args.infuse.gnss =
			{
				.flags = TASK_GNSS_FLAGS_RUN_FOREVER |
					 TASK_GNSS_FLAGS_PERFORMANCE_MODE,
			},
	},
#endif /* DT_NODE_EXISTS(DT_ALIAS(gnss)) */
	{
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 5,
		.task_logging =
			{
				{
					.loggers = STORAGE_LOGGER,
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
					.loggers = STORAGE_LOGGER,
					.tdf_mask = TASK_ENVIRONMENTAL_LOG_TPH,
				},
			},
	},
};

#if DT_NODE_EXISTS(DT_ALIAS(gnss))
#define GNSS_TASK_DEFINE (GNSS_TASK, DEVICE_DT_GET(DT_ALIAS(gnss)))
#else
#define GNSS_TASK_DEFINE
#endif

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, custom_tdf_logger),
			 (IMU_TASK, DEVICE_DT_GET(DT_ALIAS(imu0))), GNSS_TASK_DEFINE,
			 (BATTERY_TASK, DEVICE_DT_GET(DT_ALIAS(fuel_gauge0))),
			 (ENVIRONMENTAL_TASK, DEVICE_DT_GET(DT_ALIAS(environmental0)),
			  DEVICE_DT_GET_OR_NULL(DT_ALIAS(environmental1))));

static void custom_tdf_logger(uint8_t tdf_loggers, uint64_t timestamp)
{
#if defined(CONFIG_INFUSE_MEMFAULT) && DT_NODE_EXISTS(DT_CHOSEN(infuse_memfault_epacket_dump))
	if (tdf_loggers & TDF_DATA_LOGGER_UDP) {
		/* Dump any pending Memfault chunks after the UDP TDF send */
		(void)infuse_memfault_queue_dump_all(K_MSEC(50));
	}
#endif
}

#ifdef CONFIG_INFUSE_DFU_EXFAT
static void dfu_progress_cb(uint32_t copied, uint32_t total)
{
	ARG_UNUSED(copied);
	ARG_UNUSED(total);

	/* Feed all the watchdogs */
	infuse_watchdog_feed_all();
}

static void dfu_exfat_run(void)
{
	const struct device *logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
	uint8_t upgrade_partition = FIXED_PARTITION_ID(slot1_partition);
	struct infuse_version upgrade_version;

	if (dfu_exfat_app_upgrade_exists(logger, &upgrade_version) == 1) {
		LOG_INF("Upgrade image to %d.%d.%d", upgrade_version.major, upgrade_version.minor,
			upgrade_version.revision);
		if (dfu_exfat_app_upgrade_copy(logger, upgrade_version, upgrade_partition,
					       dfu_progress_cb, dfu_progress_cb) == 0) {
			LOG_INF("New image copied");
			if (boot_request_upgrade_multi(0, 0) == 0) {
				LOG_INF("Rebooting into new image");
				infuse_reboot(INFUSE_REBOOT_DFU, (uintptr_t)dfu_exfat_run, 0x00);
			}
		}
	}
}
#endif /* CONFIG_INFUSE_DFU_EXFAT */

int main(void)
{
	/* Start the watchdog */
	(void)infuse_watchdog_start();

#ifdef CONFIG_INFUSE_DFU_EXFAT
	dfu_exfat_run();
#endif /* CONFIG_INFUSE_DFU_EXFAT */

	/* Log reboot events */
	tdf_reboot_info_log(TDF_DATA_LOGGER_REMOVABLE | TDF_DATA_LOGGER_BT_ADV |
			    TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP);

	/* Configure time event logging */
	auto_time_sync_log_configure(STORAGE_LOGGER,
				     AUTO_TIME_SYNC_LOG_SYNCS | AUTO_TIME_SYNC_LOG_REBOOT_ON_SYNC);

	/* Start legacy Bluetooth advertising to workaround iOS and
	 * Nordic Softdevice connection issues.
	 */
	bluetooth_legacy_advertising_run();

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
