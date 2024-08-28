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
#include <infuse/drivers/imu.h>
#include <infuse/lib/nrf_modem_monitor.h>
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
INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_LOCATION);

IMU_SAMPLE_ARRAY_TYPE_DEFINE(imu_sample_container, 4);
ZBUS_CHAN_ID_DEFINE(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), INFUSE_ZBUS_CHAN_IMU,
		    struct imu_sample_container, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		    ZBUS_MSG_INIT(0));

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
	const struct zbus_channel *chan_bat = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	const struct zbus_channel *chan_env = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
	const struct zbus_channel *chan_loc = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION);
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	/* Reset channel stats */
	chan_bat->data->publish_count = 0;
	chan_env->data->publish_count = 0;
	chan_loc->data->publish_count = 0;
	chan_imu->data->publish_count = 0;

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_BATTERY | TASK_TDF_LOGGER_LOG_AMBIENT_ENV |
			TASK_TDF_LOGGER_LOG_LOCATION | TASK_TDF_LOGGER_LOG_ACCEL,
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
	zassert_equal(0, tdf.time);
	zassert_equal(-ENOMEM, tdf_find_in_buf(&tdf, TDF_BATTERY_STATE, pkt));
	zassert_equal(-ENOMEM, tdf_find_in_buf(&tdf, TDF_AMBIENT_TEMP_PRES_HUM, pkt));
	net_buf_unref(pkt);
}

ZTEST(task_tdf_logger, test_no_flush)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE,
		.flags = TASK_TDF_LOGGER_FLAGS_NO_FLUSH,
	};
	/* No data should be sent yet */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_is_null(pkt);

	/* Manually flush the logger */
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_ANNOUNCE, pkt));
	zassert_not_equal(0, tdf.time);
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
	zassert_within(500 * 100, time_end - time_start, 10000);
}

ZTEST(task_tdf_logger, test_battery)
{
	const struct zbus_channel *chan_bat = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_battery_state battery = {.voltage_mv = 3300, .current_ua = 100, .soc = 80};
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
	zassert_equal(0, tdf.time);
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
	zassert_equal(0, tdf.time);
	net_buf_unref(pkt);
}

ZTEST(task_tdf_logger, test_accelerometer)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct imu_sample_array *samples;
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	/* Publish data with no accelerometer */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 0;
	samples->gyroscope.num = 1;
	samples->magnetometer.num = 0;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_ACCEL,
	};

	/* Accelerometer data should not send as it doesn't exist */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_is_null(pkt);

	/* Publish data with accelerometer */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 1;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Accelerometer data should send now */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_ACC_4G, pkt));
	zassert_equal(0, tdf.time);
	net_buf_unref(pkt);

	/* Trying to send while channel is held should fail */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_SECONDS(1));
	zassert_is_null(pkt);
	zassert_equal(0, zbus_chan_finish(chan_imu));
	/* Task should have given up, not waited for over a second */
	pkt = net_buf_get(tx_queue, K_SECONDS(1));
	zassert_is_null(pkt);
}

ZTEST(task_tdf_logger, test_location)
{
	const struct zbus_channel *chan_loc = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_LOCATION);
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_gcs_wgs84_llha location = {
		.location =
			{
				.latitude = 100,
				.longitude = -200,
				.height = 33,
			},
		.h_acc = 22,
		.v_acc = 11,
	};
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	/* Publish data */
	zbus_chan_pub(chan_loc, &location, K_FOREVER);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_LOCATION,
	};
	/* Location data should send */
	task_schedule(&data);
	pkt = net_buf_get(tx_queue, K_MSEC(100));
	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_find_in_buf(&tdf, TDF_GCS_WGS84_LLHA, pkt));
	zassert_equal(0, tdf.time);
	net_buf_unref(pkt);
}

static struct signal_quality_info {
	int rc;
	int16_t rsrp;
	int8_t rsrq;
} signal_qual;

void nrf_modem_monitor_network_state(struct nrf_modem_network_state *state)
{
	*state = (struct nrf_modem_network_state){0};
}

int nrf_modem_monitor_signal_quality(int16_t *rsrp, int8_t *rsrq)
{
	*rsrp = signal_qual.rsrp;
	*rsrq = signal_qual.rsrq;
	return signal_qual.rc;
}

ZTEST(task_tdf_logger, test_net_conn)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	zassert_not_null(tx_queue);

	schedule.task_args.infuse.tdf_logger = (struct task_tdf_logger_args){
		.loggers = TDF_DATA_LOGGER_SERIAL,
		.tdfs = TASK_TDF_LOGGER_LOG_NET_CONN,
	};

	struct signal_quality_info iters[] = {
		{-EAGAIN, 0, 0},
		{0, INT16_MIN, INT8_MIN},
		{0, -100, 10},
		{0, -80, -10},
	};

	for (int i = 0; i < ARRAY_SIZE(iters); i++) {
		signal_qual = iters[i];

		/* Connection status should send */
		task_schedule(&data);
		pkt = net_buf_get(tx_queue, K_MSEC(100));
		zassert_not_null(pkt);
		net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
		zassert_equal(0, tdf_find_in_buf(&tdf, TDF_LTE_CONN_STATUS, pkt));
		zassert_equal(0, tdf.time);
		net_buf_unref(pkt);
	}
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
