/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/motion_id.h>

#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

INFUSE_STATES_ARRAY(app_states);
MOTION_ID_TASK(1, 0);
struct task_config config = MOTION_ID_TASK(0, 1);
struct task_data data;
struct task_schedule schedule = {.task_id = TASK_ID_MOTION_ID};
struct task_schedule_state state;

IMU_SAMPLE_ARRAY_TYPE_DEFINE(imu_sample_container, 20);
ZBUS_CHAN_ID_DEFINE(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), INFUSE_ZBUS_CHAN_IMU,
		    struct imu_sample_container, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		    ZBUS_MSG_INIT(0));

struct tdf_accel_config {
	uint8_t range;
	uint16_t tdf_id;
};

struct motion_id_data {
	/* The last sample value to compare between */
	struct imu_sample last_value;
	/* Reference to the task object. This allows fetching schedule data and more */
	struct task_data *task;
	/* The recently processed message count from the imu publisher */
	uint32_t publish_cnt;
	/* Threshold constant scaled to the configuration's full scale range */
	uint32_t threshold_scaled;
	/* Sensor's runtime configured full scale range */
	uint8_t range_g;
	/* Motion identification runtime state */
	uint8_t mode;
	/* Instance of first run after initialisation */
	bool first_run;
};

extern struct motion_id_data runtime_data;

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);

	/* Since there is no task handler, application states need to be manually updated after
	 * completion
	 */
	infuse_states_snapshot(app_states);
	infuse_states_tick(app_states);
	if (!atomic_test_and_set_bit(app_states, INFUSE_STATE_DEVICE_MOVING)) {
		/* Timeout occurred, sync with global states */
		infuse_state_clear(INFUSE_STATE_DEVICE_MOVING);
	}
	k_yield();
}

static void task_terminate(struct task_data *data)
{
	k_poll_signal_raise(&data->terminate_signal, 0);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
	k_yield();
}

/**
 * @brief Construct a new ZTEST covering imu-based configuration behaviour
 *
 * This test tests a range of configuration conditions such as invalid accelerometer
 * configuration, empty accelerometer values (from an IMU channel)
 *
 */
ZTEST(task_motion_id, test_task_motion_imu)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;

	/* Publish data without any accelerometer values (mag/gryo only) */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 0;
	samples->gyroscope.num = 1;
	samples->magnetometer.num = 0;

	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};

	/* Motion id should fail to initialise due to missing full scale range parameters and wait
	 * to initialise on the next message
	 */
	zassert_equal(MOTION_ID_DISABLED, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_INITIALISING, runtime_data.mode);

	/* Soft reset */
	task_terminate(&data);

	/* Publish data with an invalid accelerometer configuration */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->accelerometer.num = 5;
	samples->accelerometer.full_scale_range = 1;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Motion id should fail to initialise due to the invalid accelerometer configuration */
	zassert_equal(MOTION_ID_DISABLED, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_INITIALISING, runtime_data.mode);

	/* Submit valid values to determine when initialisation succeeds */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->accelerometer.full_scale_range = 2;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	zassert_equal(MOTION_ID_INITIALISING, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(0, infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
}

/**
 * @brief Construct a new ZTEST covering imu behaviour at runtime
 *
 * This test covers the IMU thread blocking the IMU zbus channel and later releasing it, and then
 * mimick the motion id missing a zbus channel message.
 *
 */
ZTEST(task_motion_id, test_imu_busy)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;
	uint32_t msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);

	/* Publish valid data */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 1;
	samples->accelerometer.full_scale_range = 2;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));

	zbus_chan_update_publish_metadata(chan_imu);
	msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};

	/* Expect a valid initialisation */
	zassert_equal(MOTION_ID_DISABLED, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(msg_cnt, runtime_data.publish_cnt);

	/* Hold the bus open and schedule operation*/
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(msg_cnt, runtime_data.publish_cnt);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Release with the "new" data */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	zbus_chan_update_publish_metadata(chan_imu);
	msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	zassert_equal(0, zbus_chan_finish(chan_imu));
	zassert_equal(msg_cnt - 1, runtime_data.publish_cnt);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(msg_cnt, runtime_data.publish_cnt);

	/* Release two blocks of new data */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	zbus_chan_update_publish_metadata(chan_imu);
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));
	msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	zassert_equal(msg_cnt - 2, runtime_data.publish_cnt);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(msg_cnt, runtime_data.publish_cnt);
}

/**
 * @brief Construct a new ZTEST covering imu reconfiguration behaviour at runtime
 *
 * This test covers the IMU thread switching full scale range at runtime
 *
 */
ZTEST(task_motion_id, test_imu_reconfig)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;
	uint32_t msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	uint32_t expected_threshold;

	/* Publish valid data */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 1;
	samples->accelerometer.full_scale_range = 2;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));

	zbus_chan_update_publish_metadata(chan_imu);
	msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};
	expected_threshold = schedule.task_args.infuse.motion_id.threshold_millig *
			     imu_accelerometer_1g(samples->accelerometer.full_scale_range) / 1000;

	/* Expect a valid initialisation */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(msg_cnt, runtime_data.publish_cnt);
	zassert_equal(expected_threshold, runtime_data.threshold_scaled);

	/* Now change full scale range, expect reinitialisation with correct threshold */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->accelerometer.full_scale_range = 4;

	zbus_chan_update_publish_metadata(chan_imu);
	msg_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	zassert_equal(0, zbus_chan_finish(chan_imu));
	expected_threshold = schedule.task_args.infuse.motion_id.threshold_millig *
			     imu_accelerometer_1g(samples->accelerometer.full_scale_range) / 1000;

	/* Expect a deinitialisation followed by reinitialisation with recalculated threshold */
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	task_schedule(&data);
	zassert_equal(MOTION_ID_DISABLED, runtime_data.mode);
	/* As part of reschedule, task requeues item to reinitialise. Yield to allow it to work */
	k_yield();
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_equal(expected_threshold, runtime_data.threshold_scaled);
}

/**
 * @brief Construct a new ZTEST covering full scale range values
 *
 */
ZTEST(task_motion_id, test_imu_range)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;
	uint32_t expected_threshold;
	struct tdf_accel_config configs[] = {
		{2, TDF_ACC_2G},
		{4, TDF_ACC_4G},
		{8, TDF_ACC_8G},
		{16, TDF_ACC_16G},
	};

	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 1;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};
	task_schedule(&data);

	for (int i = 0; i < ARRAY_SIZE(configs); i++) {
		/* Publish valid data */
		zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
		samples->accelerometer.full_scale_range = configs[i].range;

		zbus_chan_update_publish_metadata(chan_imu);
		zassert_equal(0, zbus_chan_finish(chan_imu));

		expected_threshold = schedule.task_args.infuse.motion_id.threshold_millig *
				     imu_accelerometer_1g(configs[i].range) / 1000;

		/* Expect a valid initialisation */
		task_schedule(&data);
		zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
		zassert_equal(expected_threshold, runtime_data.threshold_scaled);

		task_terminate(&data);
	}
}

/**
 * @brief Construct a new ZTEST covering state transitions, including timeouts
 *
 */
ZTEST(task_motion_id, test_acc_states)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;

	/* Publish data with a acc data represntative of being stationary */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 5;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));
	samples->accelerometer.full_scale_range = 2;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};

	/* Ensure moving state is false  */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Publish data with a acc data representative of being moved */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->samples[1].x = runtime_data.threshold_scaled;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure motion state is set to is moving */
	for (uint8_t i = 0; i < schedule.task_args.infuse.motion_id.in_motion_timeout; i++) {
		task_schedule(&data);
		zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
		zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	}

	/* Ensure motion state times out back to stationary */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Ensure motion state remains moving with new data */
	for (uint8_t i = 0; i < 20; i++) {
		zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
		zbus_chan_update_publish_metadata(chan_imu);
		zassert_equal(0, zbus_chan_finish(chan_imu));

		task_schedule(&data);
		zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
		zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	}

	/* Set accelerometer data to stationary and ensure movement times out corectly */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->samples[1].x = 0;
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure timeout is respected, remaining moving during timeout */
	for (uint8_t i = 0; i < schedule.task_args.infuse.motion_id.in_motion_timeout - 1; i++) {
		zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
		zbus_chan_update_publish_metadata(chan_imu);
		zassert_equal(0, zbus_chan_finish(chan_imu));

		task_schedule(&data);
		zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
		zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	}

	/* And clearing once the timeout elapses */
	for (uint8_t i = 0; i < 5; i++) {
		zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
		zbus_chan_update_publish_metadata(chan_imu);
		zassert_equal(0, zbus_chan_finish(chan_imu));

		task_schedule(&data);
		zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
		zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
	}
}

/**
 * @brief Construct a new ZTEST covering zbus message count overflow
 *
 */
ZTEST(task_motion_id, test_zbus_overflow)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;

	/* Publish data with a acc data represntative of being stationary */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 5;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));
	samples->accelerometer.full_scale_range = 2;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};

	/* Ensure moving state is false  */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Publish data with stationary acc data */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	zbus_chan_update_publish_metadata(chan_imu);
	/* Artificially increase the counter to INT32_MAX */
	chan_imu->data->publish_count = INT32_MAX;
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure moving state is still false */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Publish data with moving acc data, but the zbus channel count will overflow */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->samples[1].x = runtime_data.threshold_scaled;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure moving state is now moving */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
}

/**
 * @brief Construct a new ZTEST covering trigger values across multiple axis
 *
 */
ZTEST(task_motion_id, test_acc_trig_value)
{
	const struct zbus_channel *chan_imu = INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU);
	struct imu_sample_array *samples;

	/* Publish data with a acc data represntative of being stationary */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples = chan_imu->message;
	samples->accelerometer.num = 5;
	memset(samples->samples, 0, samples->accelerometer.num * sizeof(struct imu_sample));
	samples->accelerometer.full_scale_range = 2;
	samples->gyroscope.num = 0;
	samples->magnetometer.num = 0;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	schedule.task_args.infuse.motion_id = (struct task_motion_id_args){
		.threshold_millig = 100,
		.in_motion_timeout = 2,
	};

	/* Ensure moving state is false  */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Change movement to just below the threshold (on 1 axis) */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->samples[1].x = runtime_data.threshold_scaled - 1;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure moving state is false  */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_false(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));

	/* Change threshold to be too low individually, but exceed when axis are summed */
	zassert_equal(0, zbus_chan_claim(chan_imu, K_NO_WAIT));
	samples->samples[1].y = 1;
	zbus_chan_update_publish_metadata(chan_imu);
	zassert_equal(0, zbus_chan_finish(chan_imu));

	/* Ensure moving state is true */
	task_schedule(&data);
	zassert_equal(MOTION_ID_RUNNING, runtime_data.mode);
	zassert_true(infuse_state_get(INFUSE_STATE_DEVICE_MOVING));
}

static void motion_before(void *fixture)
{
	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);
	memset(app_states, 0, sizeof(app_states));
	if (runtime_data.mode != MOTION_ID_DISABLED) {
		task_terminate(&data);
	}
}

ZTEST_SUITE(task_motion_id, NULL, NULL, motion_before, NULL, NULL);
