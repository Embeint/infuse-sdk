/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/emul.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

#define DEV DEVICE_DT_GET_ONE(embeint_imu_emul)
IMU_TASK(1, 0, DEV);
ALG_STATIONARY_WINDOWED_TASK(1, 0);
struct task_config config[2] = {IMU_TASK(0, 1, DEV), ALG_STATIONARY_WINDOWED_TASK(0, 1)};
struct task_data data[2];
struct task_schedule schedule[2] = {{.task_id = TASK_ID_IMU}, {.task_id = TASK_ID_ALG_STATIONARY}};
struct task_schedule_state state[2];

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU_ACC_MAG);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU_ACC_MAG)

static k_tid_t task_schedule(uint8_t index)
{
	data[index].schedule_idx = index;
	data[index].executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data[index].terminate_signal);

	if (config[index].exec_type == TASK_EXECUTOR_WORKQUEUE) {
		k_work_schedule_for_queue(task_runner_work_q(),
					  &data[index].executor.workqueue.work, K_NO_WAIT);
		return NULL;
	} else {
		return k_thread_create(&data[index].executor.thread,
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
		k_work_reschedule_for_queue(task_runner_work_q(),
					    &data[index].executor.workqueue.work, K_NO_WAIT);
	}
}

static void expect_logging(uint8_t count)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt = net_buf_get(tx_queue, K_MSEC(10));
	struct tdf_acc_magnitude_std_dev *var;
	struct tdf_buffer_state state;
	struct tdf_parsed tdf;
	uint8_t found = 0;

	zassert_not_null(pkt);
	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	tdf_parse_start(&state, pkt->data, pkt->len);
	while (true) {
		if (tdf_parse(&state, &tdf) < 0) {
			break;
		}
		zassert_equal(TDF_ACC_MAGNITUDE_STD_DEV, tdf.tdf_id, "Unexpected TDF ID");
		var = tdf.data;
		printk("Count: %d StdDev: %d\n", var->count, var->std_dev);
		found += 1;
	}
	net_buf_unref(pkt);
	zassert_equal(count, found);
}

ZTEST(alg_stationary, test_send)
{
	INFUSE_STATES_ARRAY(states);
	k_tid_t imu_thread;

	schedule[0].task_args.infuse.imu = (struct task_imu_args){
		.accelerometer =
			{
				.range_g = 4,
				.rate_hz = 50,
			},
		.fifo_sample_buffer = 50,
	};
	schedule[1].task_args.infuse.alg_stationary_windowed =
		(struct task_alg_stationary_windowed_args){
			.window_seconds = 120,
			.std_dev_threshold_ug = 40000,
		};
	schedule[1].task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;
	schedule[1].task_logging[0].tdf_mask = TASK_ALG_STATIONARY_WINDOWED_LOG_WINDOW_STD_DEV;

	/* Start with lots of movement */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);

	/* Boot the algorithm thread */
	(void)task_schedule(1);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);

	/* 5 minutes, state should not be set */
	for (int i = 0; i < 5; i++) {
		zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));
		k_sleep(K_MINUTES(1));
	}

	/* Reduce the movement, let the window update */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 100);
	k_sleep(K_MINUTES(4));

	/* Stationary state should be set */
	for (int i = 0; i < 5; i++) {
		zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));
		k_sleep(K_MINUTES(1));
	}

	/* Run for 30 seconds, then change the sample rate drastically */
	k_sleep(K_SECONDS(30));
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));
	schedule[0].task_args.infuse.imu.accelerometer.rate_hz = 10;
	schedule[0].task_args.infuse.imu.fifo_sample_buffer = 10;
	imu_thread = task_schedule(0);

	/* The changed sample rate should have skipped the decision */
	k_sleep(K_MINUTES(3));
	for (int i = 0; i < 3 * SEC_PER_MIN; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
	}
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));
	k_sleep(K_MINUTES(2));
	zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));

	/* After the normal window period, the state should be cleared */
	for (int i = 0; i < 3 * SEC_PER_MIN; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
	}
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_STATIONARY));

	/* Terminate the algorithm */
	task_terminate(1);
	k_sleep(K_MSEC(10));

	/* Flush the pending TDF's */
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	expect_logging(9);
}

static void test_before(void *fixture)
{
	/* Setup links between task config and data */
	task_runner_init(schedule, state, 2, config, data, 2);
}

ZTEST_SUITE(alg_stationary, NULL, NULL, test_before, NULL, NULL);
