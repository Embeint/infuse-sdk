/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/tdf_logger.h>

TDF_LOGGER_TASK(1, 0);
struct task_config config = TDF_LOGGER_TASK(0, 1);
struct task_data data;
struct task_schedule schedule = {.task_id = TASK_ID_TDF_LOGGER};
struct task_schedule_state state;

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);

int tdf_find_in_buf(struct tdf_parsed *tdf, uint16_t tdf_id, struct net_buf *buf)
{
	struct tdf_buffer_state state;

	tdf_parse_start(&state, buf->data, buf->len);
	while (true) {
		if (tdf_parse(&state, tdf) < 0) {
			return -ENOMEM;
		}
		if (tdf->tdf_id == tdf_id) {
			return 0;
		}
	}
	return -ENOMEM;
}

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void task_terminate(struct task_data *data)
{
	k_poll_signal_raise(&data->terminate_signal, 0);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

ZTEST(task_tdf_logger, test_log_before_data)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_BATTERY | TASK_TDF_LOGGER_LOG_AMBIENT_ENV,
	};
	/* No data, no packets */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_is_null(pkt);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
	};
	/* Announce will send */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_ANNOUNCE, pkt));
	zassert_equal(-ENOMEM, tdf_find_in_buf(&tdf, TDF_BATTERY_STATE, pkt));
	zassert_equal(-ENOMEM, tdf_find_in_buf(&tdf, TDF_AMBIENT_TEMP_PRES_HUM, pkt));
	net_buf_unref(pkt);
}

ZTEST(task_tdf_logger, test_terminate)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
		.random_delay_ms = 50000,
	};

	/* Schedule with large delay */
	task_schedule(&data);
	k_sleep(K_MSEC(100));
	/* Terminate task early */
	zassert_equal(BIT(K_WORK_DELAYED_BIT),
		      k_work_delayable_busy_get(&data.executor.workqueue.work));
	task_terminate(&data);
	k_sleep(K_MSEC(100));
	/* Task should be terminated */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));
	/* Should be no data sent */
	zassert_is_null(net_buf_get(tx_queue, K_MSEC(500)));
}

ZTEST(task_tdf_logger, test_delay)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	uint32_t time_start, time_end;
	struct net_buf *pkt;

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
		.random_delay_ms = 1000,
	};

	zassert_is_null(net_buf_get(tx_queue, K_MSEC(500)));

	/* Run 100 times */
	time_start = k_uptime_get_32();
	for (int i = 0; i < 100; i++) {
		task_schedule(&data);
		pkt = net_buf_get(tx_queue, K_MSEC(1500));
		zassert_not_null(pkt);
		net_buf_unref(pkt);
	}
	time_end = k_uptime_get_32();
	/* Average delay should be 500ms */
	zassert_within(500 * 100, time_end - time_start, 5000);
}

ZTEST(task_tdf_logger, test_battery)
{
	const struct zbus_channel *chan_bat = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_battery_state battery = {.voltage_mv = 3300, .charge_ua = 100, .soc = 8000};
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	/* Publish data */
	zbus_chan_pub(chan_bat, &battery, K_FOREVER);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_BATTERY,
	};
	/* Battery data should send */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_BATTERY_STATE, pkt));
	net_buf_unref(pkt);
}

ZTEST(task_tdf_logger, test_ambient_env)
{
	const struct zbus_channel *chan_env = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_ambient_temp_pres_hum ambient = {
		.temperature = 23000, .pressure = 101000, .humidity = 5000};
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	/* Publish data */
	zbus_chan_pub(chan_env, &ambient, K_FOREVER);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_AMBIENT_ENV,
	};
	/* Ambient environmental data should send */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_AMBIENT_TEMP_PRES_HUM, pkt));
	net_buf_unref(pkt);
}

static void logger_before(void *fixture)
{
	const struct zbus_channel *chan_bat = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct zbus_channel *chan_env = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV);

	/* Reset statistics before each test */
	chan_bat->data->publish_timestamp = 0;
	chan_bat->data->publish_count = 0;
	chan_env->data->publish_timestamp = 0;
	chan_env->data->publish_count = 0;

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);
}

ZTEST_SUITE(task_tdf_logger, NULL, NULL, logger_before, NULL, NULL);
