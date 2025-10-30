/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
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
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/algorithm_runner/algorithms/demo.h>

#define DEV DEVICE_DT_GET_ONE(embeint_imu_emul)
IMU_TASK(1, 0, DEV);
struct task_config config[1] = {IMU_TASK(0, 1, DEV)};
struct task_data data[1];
struct task_schedule schedule[1] = {{.task_id = TASK_ID_IMU}};
struct task_schedule_state state[1];

ALGORITHM_DEMO_EVENT_DEFINE(test_alg_event, TDF_DATA_LOGGER_SERIAL, ALGORITHM_DEMO_EVENT_LOG, 25);
ALGORITHM_DEMO_STATE_DEFINE(test_alg_state, TDF_DATA_LOGGER_SERIAL, ALGORITHM_DEMO_STATE_LOG);
ALGORITHM_DEMO_METRIC_DEFINE(test_alg_metric, TDF_DATA_LOGGER_SERIAL, ALGORITHM_DEMO_METRIC_LOG,
			     100);

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

static int count_logging(uint16_t tdf_id)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed tdf;
	struct net_buf *pkt;
	int found = 0;

	while (true) {
		pkt = k_fifo_get(tx_queue, K_MSEC(10));
		if (pkt == NULL) {
			break;
		}
		net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

		tdf_parse_start(&state, pkt->data, pkt->len);
		while (true) {
			if (tdf_parse(&state, &tdf) < 0) {
				break;
			}
			if (tdf.tdf_id != tdf_id) {
				continue;
			}
			found += 1;
		}
		net_buf_unref(pkt);
	}

	return found;
}

ZTEST(alg_demo, test_event_generator)
{
	k_tid_t imu_thread;
	int tdfs_logged = 0;

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
	algorithm_runner_register(&test_alg_event);

	/* Start with lots of movement */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);

	/* Let it run for 1000 seconds (1000 buffers) */
	for (int i = 0; i < 20; i++) {
		tdfs_logged += count_logging(TDF_ALGORITHM_OUTPUT);
		k_sleep(K_SECONDS(50));
	}

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tdfs_logged += count_logging(TDF_ALGORITHM_OUTPUT);

	/* We expect about 250 TDFs from a 25% chance over 1000 samples */
	printk("TDFS LOGGED: %d\n", tdfs_logged);
	zassert_within(250, tdfs_logged, 50, "Unexpected number of events");
}

ZTEST(alg_demo, test_state_generator)
{
	k_tid_t imu_thread;
	int tdfs_logged;

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
	algorithm_runner_register(&test_alg_state);

	/* Start with lots of movement */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);

	/* Run for 300 seconds, periodically flushing packet buffer */
	tdfs_logged = 0;
	for (int i = 0; i < 3; i++) {
		k_sleep(K_SECONDS(100));
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
		tdfs_logged += count_logging(TDF_ALGORITHM_OUTPUT);
	}

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));

	/* Expect some minimum number of state transitions */
	zassert_true(tdfs_logged > 30, "Not enough transitions observed");
}

ZTEST(alg_demo, test_metric_generator)
{
	k_tid_t imu_thread;
	int tdfs_logged;

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
	algorithm_runner_register(&test_alg_metric);

	/* Start with lots of movement */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);

	/* Let it run for 50 seconds (50 buffers) */
	k_sleep(K_SECONDS(50));

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tdfs_logged = count_logging(TDF_ALGORITHM_OUTPUT);

	/* Expect 25 TDFs (50hz data, 100 samples per metric) */
	zassert_within(25, tdfs_logged, 1, "Unexpected number of TDFs");
}

static void test_before(void *fixture)
{
	/* Setup links between task config and data */
	task_runner_init(schedule, state, 1, config, data, 1);
}

ZTEST_SUITE(alg_demo, NULL, NULL, test_before, NULL, NULL);
