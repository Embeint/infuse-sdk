/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#include <infuse/drivers/sensor/generic_sim.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/environmental.h>

#define DEV DEVICE_DT_GET_ANY(zephyr_generic_sim_sensor)
ENVIRONMENTAL_TASK(1, 0, DEV, NULL);
struct task_config config = ENVIRONMENTAL_TASK(0, 1, DEV, NULL);
struct task_data data;
struct task_schedule schedule;
struct task_schedule_state state;

static K_SEM_DEFINE(env_published, 0, 1);

static void env_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
ZBUS_LISTENER_DEFINE(env_listener, env_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_AMBIENT_ENV), env_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)

static void env_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&env_published);
}

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void expect_logging(uint8_t log_mask, int32_t temperature, uint32_t pressure,
			   uint16_t humidity)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;
	int rc;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (log_mask == 0) {
		zassert_is_null(pkt);
		return;
	}

	zassert_not_null(pkt);

	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_AMBIENT_TEMP_PRES_HUM, &tdf);
	if (log_mask & TASK_ENVIRONMENTAL_LOG_TPH) {
		struct tdf_ambient_temp_pres_hum *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(temperature, state->temperature);
		zassert_equal(pressure, state->pressure);
		zassert_equal(humidity / 10, state->humidity);
	} else {
		zassert_equal(-ENOMEM, rc);
	}
	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_AMBIENT_TEMPERATURE, &tdf);
	if (log_mask & TASK_ENVIRONMENTAL_LOG_T) {
		struct tdf_ambient_temperature *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(temperature, state->temperature);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

static void test_env(int32_t temperature, uint32_t pressure, uint16_t humidity, uint8_t log_mask)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_AMBIENT_ENV) env_reading;
	struct sensor_value value;
	uint32_t pub_count;

	schedule.task_logging[0].tdf_mask = log_mask;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	if (temperature != 0) {
		sensor_value_from_milli(&value, temperature);
		generic_sim_channel_set(DEV, SENSOR_CHAN_AMBIENT_TEMP, value);
	}
	if (pressure != 0) {
		sensor_value_from_milli(&value, pressure);
		generic_sim_channel_set(DEV, SENSOR_CHAN_PRESS, value);
	}
	if (humidity != 0) {
		sensor_value_from_milli(&value, humidity);
		generic_sim_channel_set(DEV, SENSOR_CHAN_HUMIDITY, value);
	}

	/* Clear state */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	(void)k_sem_take(&env_published, K_NO_WAIT);

	/* Schedule task */
	task_schedule(&data);

	k_sleep(K_MSEC(500));

	/* Task should be complete */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));
	zassert_equal(pub_count + 1, zbus_chan_pub_stats_count(ZBUS_CHAN));
	zbus_chan_read(ZBUS_CHAN, &env_reading, K_FOREVER);
	zassert_equal(temperature, env_reading.temperature);
	zassert_equal(pressure, env_reading.pressure);
	zassert_equal(humidity / 10, env_reading.humidity);

	expect_logging(log_mask, temperature, pressure, humidity);
}

ZTEST(task_bat, test_only_temperature)
{

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* No channels configured, every reading should fallback to its error state */
	test_env(27123, 0, 0, 0);
}

ZTEST(task_bat, test_no_log)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	test_env(-15000, 100567, 56000, 0);
	test_env(21795, 100567, 43250, 0);
}

ZTEST(task_bat, test_log)
{
	uint8_t log_all = TASK_ENVIRONMENTAL_LOG_TPH | TASK_ENVIRONMENTAL_LOG_T;

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	test_env(37000, 101000, 12568, TASK_ENVIRONMENTAL_LOG_TPH);
	test_env(35010, 101222, 12568, TASK_ENVIRONMENTAL_LOG_TPH);

	test_env(12000, 99999, 58123, TASK_ENVIRONMENTAL_LOG_T);
	test_env(12010, 100001, 57234, TASK_ENVIRONMENTAL_LOG_T);

	test_env(-11571, 105123, 2000, log_all);
	test_env(-45002, 102009, 3123, log_all);
}

static void logger_before(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (pkt) {
		net_buf_unref(pkt);
	}
	generic_sim_reset(DEV);
	k_sem_reset(&env_published);
}

ZTEST_SUITE(task_bat, NULL, NULL, logger_before, NULL, NULL);
