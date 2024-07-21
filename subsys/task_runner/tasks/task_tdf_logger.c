/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/version.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/tasks/tdf_logger.h>

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY, INFUSE_ZBUS_CHAN_AMBIENT_ENV);
#define C_GET INFUSE_ZBUS_CHAN_GET

LOG_MODULE_REGISTER(task_tdfl, CONFIG_TASK_TDF_LOGGER_LOG_LEVEL);

static void log_announce(uint8_t loggers, uint64_t timestamp)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct infuse_version v = application_version_get();

	KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	struct tdf_announce announce = {
		.application = CONFIG_INFUSE_APPLICATION_ID,
		.version =
			{
				.major = v.major,
				.minor = v.minor,
				.revision = v.revision,
				.build_num = v.build_num,
			},
		.kv_crc = kv_store_reflect_crc(),
		.uptime = k_uptime_seconds(),
		.reboots = reboots.count,
	};

	tdf_data_logger_log(loggers, TDF_ANNOUNCE, sizeof(announce), timestamp, &announce);
}

static void log_battery(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_BATTERY
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;

	if (zbus_chan_publish_count(C_GET(INFUSE_ZBUS_CHAN_BATTERY)) == 0) {
		return;
	}
	/* Get latest value */
	zbus_chan_read(C_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	/* Add to specified loggers */
	tdf_data_logger_log(loggers, TDF_BATTERY_STATE, sizeof(battery), timestamp, &battery);
#endif
}

static void log_ambient_env(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_AMBIENT_ENV
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_AMBIENT_ENV) ambient_env;

	if (zbus_chan_publish_count(C_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)) == 0) {
		return;
	}
	/* Get latest value */
	zbus_chan_read(C_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient_env, K_FOREVER);
	/* Add to specified loggers */
	tdf_data_logger_log(loggers, TDF_AMBIENT_TEMP_PRES_HUM, sizeof(ambient_env), timestamp,
			    &ambient_env);

#endif
}

void task_tdf_logger_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_tdf_logger_args *args = &sch->task_args.infuse.tdf_logger;
	bool announce, battery, ambient_env;
	uint64_t log_timestamp;
	uint32_t delay_ms;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		return;
	}

	/* Random delay when first scheduled */
	if (task->executor.workqueue.reschedule_counter == 0 && args->random_delay_ms) {
		delay_ms = sys_rand32_get() % args->random_delay_ms;
		LOG_INF("Delaying for %d ms", delay_ms);
		task_workqueue_reschedule(task, K_MSEC(delay_ms));
		return;
	}

	announce = args->tdfs & TASK_TDF_LOGGER_LOG_ANNOUNCE;
	battery = args->tdfs & TASK_TDF_LOGGER_LOG_BATTERY;
	ambient_env = args->tdfs & TASK_TDF_LOGGER_LOG_AMBIENT_ENV;
	log_timestamp = (args->flags & TASK_TDF_LOGGER_FLAGS_NO_FLUSH) ? civil_time_now() : 0;

	LOG_INF("Ann: %d Bat: %d Env: %d", announce, battery, ambient_env);
	if (announce) {
		log_announce(args->loggers, log_timestamp);
	}
	if (battery) {
		log_battery(args->loggers, log_timestamp);
	}
	if (ambient_env) {
		log_ambient_env(args->loggers, log_timestamp);
	}

	if (!(args->flags & TASK_TDF_LOGGER_FLAGS_NO_FLUSH)) {
		/* Flush the logger to transmit */
		tdf_data_logger_flush(args->loggers);
	}
}
