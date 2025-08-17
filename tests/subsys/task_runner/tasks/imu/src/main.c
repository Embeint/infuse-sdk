/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/emul.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/imu.h>

#define DEV DEVICE_DT_GET_ONE(embeint_imu_emul)
IMU_TASK(1, 0, DEV);
struct task_config config = IMU_TASK(0, 1, DEV);
struct task_data data;
struct task_schedule schedule = {.task_id = TASK_ID_IMU};
struct task_schedule_state state;

static K_SEM_DEFINE(imu_published, 0, 1);

void imu_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU, INFUSE_ZBUS_CHAN_IMU_ACC_MAG);
ZBUS_LISTENER_DEFINE(imu_listener, imu_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), imu_listener, 5);
#define ZBUS_CHAN     INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)
#define ZBUS_MAG_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU_ACC_MAG)

void imu_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&imu_published);
}

static k_tid_t task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);

	return k_thread_create(config.executor.thread.thread, config.executor.thread.stack,
			       config.executor.thread.stack_size,
			       (k_thread_entry_t)config.executor.thread.task_fn, (void *)&schedule,
			       &data->terminate_signal, config.task_arg.arg, 5, 0, K_NO_WAIT);
}

static void task_terminate(struct task_data *data)
{
	k_poll_signal_raise(&data->terminate_signal, 0);
}

ZTEST(task_imu, test_invalid_config)
{
	k_tid_t thread;

	schedule.task_args.infuse.imu = (struct task_imu_args){
		.accelerometer =
			{
				.range_g = 3,
				.rate_hz = 50,
			},
		.fifo_sample_buffer = 10,
	};

	/* Schedule with invalid range */
	thread = task_schedule(&data);

	/* No data should be published*/
	zassert_equal(-EAGAIN, k_sem_take(&imu_published, K_SECONDS(2)));
	/* Thread should have terminated */
	zassert_equal(0, k_thread_join(thread, K_NO_WAIT));
}

static void expect_logging(uint8_t range, bool expect_idx_metadata)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt = k_fifo_get(tx_queue, K_MSEC(10));
	struct tdf_idx_array_period *idx_array;
	struct tdf_parsed tdf;
	uint16_t expected_tdf;
	int rc;

	switch (range) {
	case 2:
		expected_tdf = TDF_ACC_2G;
		break;
	case 4:
		expected_tdf = TDF_ACC_4G;
		break;
	case 8:
		expected_tdf = TDF_ACC_8G;
		break;
	default:
		expected_tdf = TDF_ACC_16G;
		break;
	}

	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, expected_tdf, &tdf);
	zassert_equal(0, rc);
	if (IS_ENABLED(CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY)) {
		zassert_equal(TDF_DATA_FORMAT_IDX_ARRAY, tdf.data_type);
		if (expect_idx_metadata) {
			zassert_not_equal(0, tdf.time);
		} else {
			zassert_equal(0, tdf.time);
		}
	} else if (IS_ENABLED(CONFIG_TASK_RUNNER_TASK_IMU_LOG_DIFF_ARRAY)) {
		/* Real data may show up as TIME_ARRAY or SINGLE, but the simulated data is
		 * always close enough to be encoded as a diff.
		 */
		zassert_equal(TDF_DATA_FORMAT_DIFF_ARRAY_16_8, tdf.data_type);
	} else {
		zassert_true((TDF_DATA_FORMAT_TIME_ARRAY == tdf.data_type) ||
			     (TDF_DATA_FORMAT_SINGLE == tdf.data_type));
	}
	zassert_equal(6, tdf.tdf_len);

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_IDX_ARRAY_PERIOD, &tdf);
	if (expect_idx_metadata) {
		zassert_equal(0, rc);
		idx_array = tdf.data;
		zassert_equal(expected_tdf, idx_array->tdf_id);
		zassert_not_equal(0, idx_array->period);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

static void test_imu(uint8_t range, uint16_t rate, uint16_t num_samples, uint8_t num_buffers,
		     bool log)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	const struct imu_magnitude_array *magnitudes;
	const struct imu_sample_array *samples;
	bool idx_metadata;
	uint16_t one_g;
	k_tid_t thread;

	schedule.task_args.infuse.imu = (struct task_imu_args){
		.accelerometer =
			{
				.range_g = range,
				.rate_hz = rate,
			},
		.fifo_sample_buffer = num_samples,
	};
	schedule.task_logging[0].tdf_mask = 0;
	schedule.task_logging[0].loggers = 0;
	if (log) {
		schedule.task_logging[0].tdf_mask = TASK_IMU_LOG_ACC;
		schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	}

	(void)k_sem_take(&imu_published, K_NO_WAIT);
	zassert_not_null(tx_queue);
	thread = task_schedule(&data);

	for (int i = 0; i < num_buffers; i++) {
		/* Wait for emulated data */
		zassert_equal(0, k_sem_take(&imu_published, K_SECONDS(1)));

		/* Check published IMU data */
		zassert_equal(0, zbus_chan_claim(ZBUS_CHAN, K_MSEC(1)));
		samples = ZBUS_CHAN->message;
		zassert_equal(range, samples->accelerometer.full_scale_range);
		zassert_equal(num_samples, samples->accelerometer.num);
		zassert_equal(0, samples->gyroscope.num);
		zassert_equal(0, samples->magnetometer.num);
		zassert_equal(0, samples->accelerometer.offset);
		zbus_chan_finish(ZBUS_CHAN);

		/* Check published accelerometer magnitude data */
		zassert_equal(0, zbus_chan_claim(ZBUS_MAG_CHAN, K_MSEC(1)));
		magnitudes = ZBUS_MAG_CHAN->message;
		one_g = imu_accelerometer_1g(range);
		zassert_equal(range, magnitudes->meta.full_scale_range);
		zassert_equal(num_samples, magnitudes->meta.num);
		/* Within 5% (variance is due to the injected noise) */
		for (int i = 0; i < magnitudes->meta.num; i++) {
			zassert_within(magnitudes->magnitudes[i], one_g, one_g / 20);
		}
		zbus_chan_finish(ZBUS_MAG_CHAN);

		/* No data being pushed */
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
		if (log == 0) {
			zassert_is_null(k_fifo_get(tx_queue, K_MSEC(1)));
		} else {
			idx_metadata =
				IS_ENABLED(CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY) && i == 0;
			expect_logging(range, idx_metadata);
		}
	}

	task_terminate(&data);
	zassert_equal(0, k_thread_join(thread, K_SECONDS(2)));
}

ZTEST(task_imu, test_no_log)
{
	imu_emul_accelerometer_data_configure(DEV, 1.0f, 0.0f, 0.0f, 0);
	test_imu(4, 50, 25, 5, false);
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 1.0f, 0.0f, 10);
	test_imu(2, 25, 20, 5, false);
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 100);
	test_imu(8, 30, 15, 5, false);
}

ZTEST(task_imu, test_log)
{
	imu_emul_accelerometer_data_configure(DEV, 1.0f, 0.0f, 0.0f, 0);
	test_imu(4, 50, 25, 5, true);
	imu_emul_accelerometer_data_configure(DEV, 0.707f, 0.707f, 0.0f, 15);
	test_imu(2, 25, 20, 5, true);
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.707f, -0.707f, 33);
	test_imu(8, 30, 15, 5, true);
}

ZTEST(task_imu, test_imu_timestamp)
{
	/* Sample period of 100 ticks */
	struct imu_sensor_meta meta1 = {
		.timestamp_ticks = 10000,
		.buffer_period_ticks = 900,
		.num = 10,
	};

	zassert_equal(100, imu_sample_period(&meta1));
	zassert_equal(CONFIG_SYS_CLOCK_TICKS_PER_SEC / 100, imu_sample_rate(&meta1));
	for (int i = 0; i < 10; i++) {
		zassert_equal(10000 + (i * 100), imu_sample_timestamp(&meta1, i));
	}

	/* Sample period of 33 ticks */
	struct imu_sensor_meta meta2 = {
		.timestamp_ticks = 10000,
		.buffer_period_ticks = 297,
		.num = 10,
	};

	zassert_equal(33, imu_sample_period(&meta2));
	zassert_equal(CONFIG_SYS_CLOCK_TICKS_PER_SEC / 33, imu_sample_rate(&meta2));
	for (int i = 0; i < 10; i++) {
		zassert_equal(10000 + (i * 33), imu_sample_timestamp(&meta2, i));
	}

	/* Single sample */
	struct imu_sensor_meta meta_single = {
		.timestamp_ticks = 567,
		.buffer_period_ticks = 100,
		.num = 1,
	};

	zassert_equal(0, imu_sample_period(&meta_single));
	zassert_equal(0, imu_sample_rate(&meta_single));
	zassert_equal(567, imu_sample_timestamp(&meta_single, 0));
}

static void logger_before(void *fixture)
{
	k_sem_reset(&imu_published);
	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);
}

ZTEST_SUITE(task_imu, NULL, NULL, logger_before, NULL, NULL);
