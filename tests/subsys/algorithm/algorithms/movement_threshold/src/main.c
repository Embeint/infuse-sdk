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
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

#include <infuse/algorithm_runner/runner.h>
#include <infuse/algorithm_runner/algorithms/movement_threshold.h>

#define DEV DEVICE_DT_GET_ONE(embeint_imu_emul)
IMU_TASK(1, 0, DEV);
struct task_config config[1] = {IMU_TASK(0, 1, DEV)};
struct task_data data[1];
struct task_schedule schedule[1] = {{.task_id = TASK_ID_IMU}};
struct task_schedule_state state[1];

ALGORITHM_MOVEMENT_THRESHOLD_DEFINE(test_alg, 3, 40000, 40000);

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

static int moving_count;
static int stopped_count;

static void state_set(enum infuse_state state, bool already, uint16_t timeout, void *user_ctx)
{
	switch (state) {
	case INFUSE_STATE_DEVICE_STARTED_MOVING:
		zassert_equal(1, timeout);
		moving_count += 1;
		break;
	case INFUSE_STATE_DEVICE_STOPPED_MOVING:
		zassert_equal(1, timeout);
		stopped_count += 1;
		break;
	default:
		break;
	}
}

ZTEST(alg_movement_threshold, test_impl)
{
	INFUSE_STATES_ARRAY(states);
	struct infuse_state_cb state_cb = {
		.state_set = state_set,
	};
	k_tid_t imu_thread;

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
	infuse_state_register_callback(&state_cb);

	/* Start with no movement */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 1);

	/* Boot the IMU data generator */
	imu_thread = task_schedule(0);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* No states set to start with */
	k_sleep(K_SECONDS(5));
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	zassert_equal(0, moving_count);

	for (int i = 0; i < 3; i++) {
		/* Increase noise (device moving) */
		imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);
		k_sleep(K_SECONDS(1));

		/* State stays set while moving */
		for (int j = 0; j < 5; j++) {
			k_sleep(K_SECONDS(1));
			infuse_states_snapshot(states);
			infuse_states_tick(states);
			zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
			zassert_equal(i + 1, moving_count);
		}

		/* Return to no moving */
		imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 1);
		k_sleep(K_SECONDS(1));

		/* State returns to not set on the second iteration.
		 * Tick 1 happens in the previous loop
		 * Tick 2 happens on first iteration of this loop
		 * Tick 3 clears the state
		 */
		for (int j = 0; j < 5; j++) {
			k_sleep(K_SECONDS(1));
			infuse_states_snapshot(states);
			infuse_states_tick(states);
			zassert_equal(j < 1, infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
			zassert_equal(i + 1, moving_count);
		}
	}

	/* Overwrite configuration with a threshold > 1G */
	struct kv_alg_movement_threshold_args_v2 kv_config = {
		.args =
			{

				.moving_for = 4,
				.initial_threshold_ug = 1500000,
				.continue_threshold_ug = 1500000,
			},
	};

	zassert_equal(sizeof(kv_config),
		      KV_STORE_WRITE(KV_KEY_ALG_MOVEMENT_THRESHOLD_ARGS_V2, &kv_config));
	k_sleep(K_SECONDS(2));

	/* Same noise as before no longer moving */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 800);
	k_sleep(K_SECONDS(3));
	zassert_equal(3, moving_count);

	/* Much larger sample variation */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 10000);
	k_sleep(K_SECONDS(3));
	zassert_equal(4, moving_count);

	/* New configuration is active for 4 seconds */
	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 1);
	for (int j = 0; j < 3; j++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	}
	infuse_states_snapshot(states);
	infuse_states_tick(states);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	zassert_equal(4, moving_count);

	/* Overwrite configuration with differing initial and continue thresholds (0.4G vs 0.9G) */
	struct kv_alg_movement_threshold_args_v2 kv_config2 = {
		.args =
			{

				.moving_for = 4,
				.initial_threshold_ug = 400000,
				.continue_threshold_ug = 900000,
			},
	};
	zassert_equal(sizeof(kv_config2),
		      KV_STORE_WRITE(KV_KEY_ALG_MOVEMENT_THRESHOLD_ARGS_V2, &kv_config2));
	k_sleep(K_SECONDS(2));

	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 5000);

	for (int i = 0; i < 10; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		k_sleep(K_SECONDS(1));
	}
	infuse_states_snapshot(states);
	infuse_states_tick(states);

	/* Despite accelerometer variance not changing, should hae transitioned between moving and
	 * not moving due to the different thresholds. This is easier than testing the reverse
	 * condition (lower continue threshold).
	 */
	zassert_equal(7, moving_count);

	imu_emul_accelerometer_data_configure(DEV, 0.0f, 0.0f, 1.0f, 1);

	/* Terminate the IMU producer */
	task_terminate(0);
	zassert_equal(0, k_thread_join(imu_thread, K_SECONDS(2)));

	/* Unregister callback */
	infuse_state_unregister_callback(&state_cb);
}

static void test_before(void *fixture)
{
	/* Setup links between task config and data */
	task_runner_init(schedule, state, 1, config, data, 1);
}

ZTEST_SUITE(alg_movement_threshold, NULL, NULL, test_before, NULL, NULL);
