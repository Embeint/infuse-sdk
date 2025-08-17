/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/work_q.h>
#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/emul.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/algorithm_runner/algorithms/tilt.h>

#define DEV DEVICE_DT_GET_ONE(embeint_imu_emul)
IMU_TASK(1, 0, DEV);
struct task_config config[1] = {IMU_TASK(0, 1, DEV)};
struct task_data data[1];
struct task_schedule schedule[1] = {{.task_id = TASK_ID_IMU}};
struct task_schedule_state state[1];

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_TILT);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_TILT)

ALGORITHM_TILT_DEFINE(test_alg, TDF_DATA_LOGGER_SERIAL, ALGORITHM_TILT_LOG_ANGLE, 0.025f, 10);

static k_tid_t task_schedule(uint8_t index)
{
	data[index].schedule_idx = index;
	data[index].executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data[index].terminate_signal);

	if (config[index].exec_type == TASK_EXECUTOR_WORKQUEUE) {
		infuse_work_schedule(&data[index].executor.workqueue.work, K_NO_WAIT);
		return NULL;
	} else {
		return k_thread_create(config[index].executor.thread.thread,
				       config[index].executor.thread.stack,
				       config[index].executor.thread.stack_size,
				       (k_thread_entry_t)config[index].executor.thread.task_fn,
				       (void *)&schedule[index], &data[index].terminate_signal,
				       config[index].task_arg.arg, 5, 0, K_NO_WAIT);
	}
}

static void task_terminate(uint8_t index)
{
	k_poll_signal_raise(&data[index].terminate_signal, 0);
	if (config[index].exec_type == TASK_EXECUTOR_WORKQUEUE) {
		infuse_work_reschedule(&data[index].executor.workqueue.work, K_NO_WAIT);
	}
}

static float expect_logging(uint8_t count)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt = k_fifo_get(tx_queue, K_MSEC(10));
	struct tdf_device_tilt *var;
	struct tdf_buffer_state state;
	struct tdf_parsed tdf;
	uint8_t found = 0;
	float last = 0.0f;

	if (count == 0) {
		zassert_is_null(pkt);
		return 0.0f;
	}

	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	tdf_parse_start(&state, pkt->data, pkt->len);
	while (true) {
		if (tdf_parse(&state, &tdf) < 0) {
			break;
		}
		zassert_equal(TDF_DEVICE_TILT, tdf.tdf_id, "Unexpected TDF ID");
		var = tdf.data;
		last = var->cosine;
		found += 1;
	}
	net_buf_unref(pkt);
	zassert_equal(count, found);

	return last;
}

ZTEST(alg_stationary, test_send)
{
	KV_KEY_TYPE(KV_KEY_GRAVITY_REFERENCE) gravity;
	struct infuse_zbus_chan_tilt *out = ZBUS_CHAN->message;
	int64_t timeout_base;
	k_tid_t imu_thread;
	float last;

	schedule[0].task_args.infuse.imu = (struct task_imu_args){
		.accelerometer =
			{
				.range_g = 4,
				.rate_hz = 50,
			},
		.fifo_sample_buffer = 50,
	};

	/* Initialise algorithm runner */
	algorithm_runner_init();
	algorithm_runner_register(&test_alg);

	/* Start with gravity aligned to Z axis */
	gravity.x = 0;
	gravity.y = 0;
	gravity.z = -8192;
	zassert_equal(sizeof(gravity), KV_STORE_WRITE(KV_KEY_GRAVITY_REFERENCE, &gravity));

	/* Start with accelerometer aligned to gravity */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, -1.0f, 0);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);
	timeout_base = k_uptime_get();

	/* 0 degree tilt */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, -1.0f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 10100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(1.000f, last, 0.001f);
	zassert_within(1.000f, out->cosine, 0.001f);
	zassert_equal(10, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* 45 degree tilt */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, -0.707f, -0.707f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 20100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(0.707f, last, 0.001f);
	zassert_within(0.707f, out->cosine, 0.001f);
	zassert_equal(20, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* 90 degree tilt */
	imu_emul_accelerometer_data_configure(DEV, -1.0f, 0.0f, 0.0f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 30100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(0.000f, last, 0.001f);
	zassert_within(0.000f, out->cosine, 0.001f);
	zassert_equal(30, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* 135 degree tilt */
	imu_emul_accelerometer_data_configure(DEV, -0.707f, 0.0f, 0.707f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 40100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(-0.707f, last, 0.001f);
	zassert_within(-0.707f, out->cosine, 0.001f);
	zassert_equal(40, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* 180 degree tilt */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 50100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(-1.000f, last, 0.001f);
	zassert_within(-1.000f, out->cosine, 0.001f);
	zassert_equal(50, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Update the reference vector */
	gravity.x = 0;
	gravity.y = 8192;
	gravity.z = 0;
	zassert_equal(sizeof(gravity), KV_STORE_WRITE(KV_KEY_GRAVITY_REFERENCE, &gravity));

	/* Angle should now be 90 degrees */
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 60100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(0.000f, last, 0.001f);
	zassert_within(0.000f, out->cosine, 0.001f);
	zassert_equal(60, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Delete the reference vector */
	zassert_equal(0, kv_store_delete(KV_KEY_GRAVITY_REFERENCE));

	/* No more data logged */
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 70100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(0);
	zassert_within(0.000f, out->cosine, 0.001f);
	zassert_equal(60, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Reference vector restored */
	gravity.x = 1000;
	gravity.y = -1000;
	gravity.z = 0;
	zassert_equal(sizeof(gravity), KV_STORE_WRITE(KV_KEY_GRAVITY_REFERENCE, &gravity));

	/* Some angle */
	imu_emul_accelerometer_data_configure(DEV, 0.3f, -0.1f, 0.9f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 80100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(0.296f, last, 0.001f);
	zassert_within(0.296f, out->cosine, 0.001f);
	zassert_equal(70, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Device is moving (magnitude is outside 10% of 1G)
	 * No more data published, channel data stays the same
	 */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 0.89f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 90100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(0);
	zassert_within(0.296f, out->cosine, 0.001f);
	zassert_equal(70, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Stationary again */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.1f, 1.0f, 0);
	k_sleep(K_TIMEOUT_ABS_MS(timeout_base + 100100));
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	last = expect_logging(10);
	zassert_within(-0.070f, out->cosine, 0.001f);
	zassert_equal(80, zbus_chan_pub_stats_count(ZBUS_CHAN));

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_MSEC(1000)));
}

static void test_before(void *fixture)
{
	/* Setup links between task config and data */
	task_runner_init(schedule, state, 1, config, data, 1);
}

static void test_after(void *fixture)
{
	/* Terminate IMU producer if its still running */
	task_terminate(0);
}

ZTEST_SUITE(alg_stationary, NULL, NULL, test_before, test_after, NULL);
