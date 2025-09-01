/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/version.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu.h>
#include <infuse/lib/nrf_modem_monitor.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/math/common.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/tasks/tdf_logger.h>

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY, INFUSE_ZBUS_CHAN_AMBIENT_ENV,
			 INFUSE_ZBUS_CHAN_LOCATION, INFUSE_ZBUS_CHAN_IMU);
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

#if defined(CONFIG_DATA_LOGGER_EXFAT) || defined(CONFIG_DATA_LOGGER_FLASH_MAP)
#if defined(CONFIG_DATA_LOGGER_EXFAT)
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));

	announce.flags |= 0x01;
#else
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
#endif
	struct data_logger_state state;

	if (device_is_ready(logger)) {
		data_logger_get_state(logger, &state);
		announce.blocks = state.current_block;
	} else {
		announce.blocks = UINT32_MAX;
	}
#endif /* defined(CONFIG_DATA_LOGGER_EXFAT) || defined(CONFIG_DATA_LOGGER_FLASH_MAP) */

	TDF_DATA_LOGGER_LOG(loggers, TDF_ANNOUNCE, timestamp, &announce);
}

static void log_battery(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_BATTERY
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery;

	if (zbus_chan_pub_stats_count(C_GET(INFUSE_ZBUS_CHAN_BATTERY)) == 0) {
		return;
	}
	/* Get latest value */
	zbus_chan_read(C_GET(INFUSE_ZBUS_CHAN_BATTERY), &battery, K_FOREVER);
	/* Add to specified loggers */
#if defined(CONFIG_TASK_TDF_LOGGER_BATTERY_TYPE_COMPLETE)
	TDF_DATA_LOGGER_LOG(loggers, TDF_BATTERY_STATE, timestamp, &battery);
#elif defined(CONFIG_TASK_TDF_LOGGER_BATTERY_TYPE_VOLTAGE)
	struct tdf_battery_voltage tdf = {.voltage = battery.voltage_mv};

	TDF_DATA_LOGGER_LOG(loggers, TDF_BATTERY_VOLTAGE, timestamp, &tdf);
#elif defined(CONFIG_TASK_TDF_LOGGER_BATTERY_TYPE_SOC)
	struct tdf_battery_soc tdf = {.soc = battery.soc};

	TDF_DATA_LOGGER_LOG(loggers, TDF_BATTERY_SOC, timestamp, &tdf);
#else
#error Unknown battery logging type
#endif
#endif
}

static void log_ambient_env(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_AMBIENT_ENV
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_AMBIENT_ENV) ambient_env;

	if (infuse_zbus_channel_data_age(C_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)) >=
	    (CONFIG_TASK_TDF_LOGGER_ENVIRONMENTAL_TIMEOUT_SEC * MSEC_PER_SEC)) {
		return;
	}
	/* Get latest value */
	zbus_chan_read(C_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV), &ambient_env, K_FOREVER);

	/* Add to specified loggers */
	if ((ambient_env.pressure == 0) && (ambient_env.humidity == 0)) {
		struct tdf_ambient_temperature temp = {.temperature = ambient_env.temperature};

		TDF_DATA_LOGGER_LOG(loggers, TDF_AMBIENT_TEMPERATURE, timestamp, &temp);
	} else {
		TDF_DATA_LOGGER_LOG(loggers, TDF_AMBIENT_TEMP_PRES_HUM, timestamp, &ambient_env);
	}
#endif
}

static void log_location(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_LOCATION
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_LOCATION) location;

	if (infuse_zbus_channel_data_age(C_GET(INFUSE_ZBUS_CHAN_LOCATION)) >=
	    (CONFIG_TASK_TDF_LOGGER_LOCATION_TIMEOUT_SEC * MSEC_PER_SEC)) {
		return;
	}
	/* Get latest value */
	zbus_chan_read(C_GET(INFUSE_ZBUS_CHAN_LOCATION), &location, K_FOREVER);
	/* Add to specified loggers */
	TDF_DATA_LOGGER_LOG(loggers, TDF_GCS_WGS84_LLHA, timestamp, &location);
#endif
}

static void log_accel(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_INFUSE_ZBUS_CHAN_IMU
	struct imu_sample_array *imu;
	struct imu_sample *sample;
	struct tdf_struct_xyz_16bit tdf;
	uint16_t tdf_id;

	if (infuse_zbus_channel_data_age(C_GET(INFUSE_ZBUS_CHAN_IMU)) >=
	    (CONFIG_TASK_TDF_LOGGER_IMU_TIMEOUT_SEC * MSEC_PER_SEC)) {
		return;
	}
	/* Accept waiting for a short duration to get the channel data */
	if (zbus_chan_claim(C_GET(INFUSE_ZBUS_CHAN_IMU), K_MSEC(100)) < 0) {
		return;
	}
	imu = C_GET(INFUSE_ZBUS_CHAN_IMU)->message;
	if (imu->accelerometer.num == 0) {
		/* No accelerometer values, release and return */
		zbus_chan_finish(C_GET(INFUSE_ZBUS_CHAN_IMU));
		return;
	}
	/* Extract sample into TDF */
	sample = &imu->samples[imu->accelerometer.offset + imu->accelerometer.num - 1];
	tdf_id = tdf_id_from_accelerometer_range(imu->accelerometer.full_scale_range);
	tdf.x = sample->x;
	tdf.y = sample->y;
	tdf.z = sample->z;

	/* Release channel */
	zbus_chan_finish(C_GET(INFUSE_ZBUS_CHAN_IMU));
	/* Add to specified loggers */
	tdf_data_logger_log(loggers, tdf_id, sizeof(tdf), timestamp, &tdf);
#endif
}

static void log_network_connection(uint8_t loggers, uint64_t timestamp)
{
#ifdef CONFIG_TASK_RUNNER_TASK_TDF_LOGGER_NRF_MODEM_MONITOR
	struct tdf_lte_conn_status tdf;
	struct nrf_modem_network_state state;
	int16_t rsrp;
	int8_t rsrq;

	/* Query LTE network state */
	nrf_modem_monitor_network_state(&state);
	(void)nrf_modem_monitor_signal_quality(&rsrp, &rsrq, true);
	/* Convert to TDF */
	tdf_lte_conn_status_from_monitor(&state, &tdf, rsrp, rsrq);
	/* Add to specified loggers */
	TDF_DATA_LOGGER_LOG(loggers, TDF_LTE_CONN_STATUS, timestamp, &tdf);
#endif
}

void task_tdf_logger_manual_run(uint8_t tdf_loggers, uint64_t timestamp, uint16_t tdfs,
				tdf_logger_custom_log_t custom_logger)
{
	bool announce, battery, ambient_env, location, accel, net, custom;

	announce = tdfs & TASK_TDF_LOGGER_LOG_ANNOUNCE;
	battery = tdfs & TASK_TDF_LOGGER_LOG_BATTERY;
	ambient_env = tdfs & TASK_TDF_LOGGER_LOG_AMBIENT_ENV;
	location = tdfs & TASK_TDF_LOGGER_LOG_LOCATION;
	accel = tdfs & TASK_TDF_LOGGER_LOG_ACCEL;
	net = tdfs & TASK_TDF_LOGGER_LOG_NET_CONN;
	custom = tdfs & TASK_TDF_LOGGER_LOG_CUSTOM;

	if ((tdf_loggers == TDF_DATA_LOGGER_BT_ADV) ||
	    (tdf_loggers == TDF_DATA_LOGGER_BT_PERIPHERAL)) {
		/* Bluetooth can log very often */
		LOG_DBG("Log: %02X Ann: %d Bat: %d Env: %d Loc: %d Acc: %d Net: %d Cus: %d",
			tdf_loggers, announce, battery, ambient_env, location, accel, net, custom);
	} else {
		LOG_INF("Log: %02X Ann: %d Bat: %d Env: %d Loc: %d Acc: %d Net: %d Cus: %d",
			tdf_loggers, announce, battery, ambient_env, location, accel, net, custom);
	}

	if (announce) {
		log_announce(tdf_loggers, timestamp);
	}
	if (battery) {
		log_battery(tdf_loggers, timestamp);
	}
	if (ambient_env) {
		log_ambient_env(tdf_loggers, timestamp);
	}
	if (accel) {
		log_accel(tdf_loggers, timestamp);
	}
	if (location) {
		log_location(tdf_loggers, timestamp);
	}
	if (net) {
		log_network_connection(tdf_loggers, timestamp);
	}
	if (custom && (custom_logger != NULL)) {
		custom_logger(tdf_loggers, timestamp);
	}
}

void task_tdf_logger_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_tdf_logger_args *args = &sch->task_args.infuse.tdf_logger;
	uint8_t *persistent = task_schedule_persistent_storage(task);
	uint64_t log_timestamp;
	uint32_t delay_ms;
	uint16_t tdfs;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		return;
	}

	/* Random delay when first scheduled */
	if (task->executor.workqueue.reschedule_counter == 0 && args->random_delay_ms) {
		delay_ms = sys_rand32_get() % args->random_delay_ms;
		LOG_DBG("Delaying for %d ms", delay_ms);
		task_workqueue_reschedule(task, K_MSEC(delay_ms));
		return;
	}

	log_timestamp = (args->flags & TASK_TDF_LOGGER_FLAGS_NO_FLUSH) ? epoch_time_now() : 0;
	tdfs = args->tdfs;

	/* Handle iteration (persistent storage used as state storage) */
	if (args->per_run != 0) {
		tdfs = math_bitmask_get_next_bits(tdfs, persistent[0], persistent, args->per_run);
	}

	/* Run the logging function */
	task_tdf_logger_manual_run(args->loggers, log_timestamp, tdfs,
				   task->executor.workqueue.task_arg.const_arg);

	if (!(args->flags & TASK_TDF_LOGGER_FLAGS_NO_FLUSH)) {
		/* Flush the logger to transmit */
		tdf_data_logger_flush(args->loggers);
	}

	/* Reschedule next log */
	if (args->logging_period_ms) {
		delay_ms = args->logging_period_ms + (sys_rand32_get() % args->random_delay_ms);
		LOG_DBG("Rescheduling for %d ms", delay_ms);
		task_workqueue_reschedule(task, K_MSEC(delay_ms));
	}
}
