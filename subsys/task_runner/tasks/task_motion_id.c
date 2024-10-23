/**
 * @file task_motion_id.c
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu.h>
#include <infuse/math/common.h>
#include <infuse/states.h>
#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/motion_id.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>
#include <infuse/version.h>
#include <infuse/zbus/channels.h>

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

LOG_MODULE_REGISTER(task_motion_id, CONFIG_TASK_MOTION_ID_LOG_LEVEL);

/**
 * @brief Motion identification runtime state data
 *
 */
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

void imu_new_data_cb(const struct zbus_channel *chan);

struct motion_id_data runtime_data;
ZBUS_LISTENER_DEFINE_WITH_ENABLE(imu_listener, imu_new_data_cb, false);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), imu_listener, 3);

static inline int is_new_data_available(struct motion_id_data *data, uint32_t new_publish_count)
{
	/* New data is available when the publish count is not the last known publish count was */
	return data->publish_cnt != new_publish_count;
}

static int motion_id_initialise(bool as_listener)
{
	const struct imu_sample_array *imu;
	int rc = 0;
	uint32_t current_pub_cnt;
	int16_t accel_1g;

	/* Get the message containing the imu data to get metadata such as full scale range */
	if (as_listener) {
		imu = ZBUS_CHAN->message;
	} else {
		if (zbus_chan_claim(ZBUS_CHAN, K_MSEC(100)) < 0) {
			LOG_DBG("Failed to claim IMU data while initialising. Trying again later");
			return -EBUSY;
		}
		imu = ZBUS_CHAN->message;
	}

	/* Skip if this data has already been observed */
	current_pub_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	if (!is_new_data_available(&runtime_data, current_pub_cnt)) {
		rc = -EAGAIN;
		goto init_end;
	}
	runtime_data.publish_cnt = current_pub_cnt;

	/* Ensure there is accelerometer data present */
	if (!imu->accelerometer.num) {
		/* There is no accelerometer data, wait for the next message */
		LOG_DBG("IMU data was available, but did not contains any accelerometer values");
		runtime_data.publish_cnt = 0;
		rc = -EBADF;
		goto init_end;
	}

	/* Use the reported range to calculate required thresholds */
	runtime_data.range_g = imu->accelerometer.full_scale_range;
	accel_1g = imu_accelerometer_1g(runtime_data.range_g);
	if (accel_1g < 0) {
		LOG_WRN("Invalid IMU Accelerometer full scale range '%d'. for '%d' samples",
			runtime_data.range_g, imu->accelerometer.num);
		rc = -EINVAL;
		goto init_end;
	}

	/* The value currently in threshold scaled was set on task startup and is currently unscaled
	 */
	runtime_data.threshold_scaled = ((uint32_t)accel_1g * runtime_data.threshold_scaled) / 1000;
	runtime_data.last_value = imu->samples[imu->accelerometer.offset];
	/* Leave this msg unread, so it's processed for movement */
	runtime_data.publish_cnt = current_pub_cnt - 1;
	runtime_data.first_run = true;
	runtime_data.mode = MOTION_ID_RUNNING;

init_end:
	if (!as_listener) {
		zbus_chan_finish(ZBUS_CHAN);
	}
	return rc;
}

static int motion_process(void)
{
	const struct imu_sample_array *imu;
	const struct task_schedule *sch;
	const struct imu_sample *s1;
	const struct imu_sample *s2;
	int rc = 0;
	int32_t delta;
	uint32_t total_delta;
	uint32_t current_pub_cnt;

	/* Check there is new data (publish count is higher or overflow) */
	current_pub_cnt = zbus_chan_publish_count(ZBUS_CHAN);
	if (!is_new_data_available(&runtime_data, current_pub_cnt)) {
		/* There is no new data - wait for next runtime */
		return -EAGAIN;
	}

	/* Get the message containing the imu data */
	rc = zbus_chan_claim(ZBUS_CHAN, K_MSEC(100));
	if (rc < 0) {
		LOG_DBG("Failed to claim IMU data while initialising. Trying again later");
		return -EBUSY;
	}

	imu = ZBUS_CHAN->message;

	/* Ensure the full scale range is the same */
	if (imu->accelerometer.full_scale_range != runtime_data.range_g) {
		/* Full scale range changed between runs. reinitialisation is required */
		runtime_data.mode = MOTION_ID_DISABLED;
		task_workqueue_reschedule(runtime_data.task, K_NO_WAIT);
		LOG_INF("Accelerometer full scale range changed. Reinitialising motion id");
		goto process_end;
	}

	runtime_data.publish_cnt = current_pub_cnt;
	/* Ensure accelerometer values are available */
	if (!imu->accelerometer.num) {
		rc = -EBADF;
		goto process_end;
	}

	/* Check if the edge samples of each buffer should be compared */
	if (runtime_data.publish_cnt + 1 == current_pub_cnt && !runtime_data.first_run) {
		/* Check values between end and start values of sequential buffers */
		s2 = &runtime_data.last_value;
	} else {
		/* Otherwise, only search for movement within the buffer */
		s2 = &imu->samples[0];
	}
	runtime_data.first_run = false;

	/* This is supposed to be lightweight, so Manhattan distance is probably fine, when compared
	 * to Euclidean distance when determinig the total magntiude of instantaneous acceleration
	 */
	for (uint8_t i = 0; i < imu->accelerometer.num; i++) {
		s1 = s2;
		s2 = &imu->samples[i + imu->accelerometer.offset];

		delta = ((int32_t)s2->x) - ((int32_t)s1->x);
		total_delta = math_abs(delta);
		delta = ((int32_t)s2->y) - ((int32_t)s1->y);
		total_delta += math_abs(delta);
		delta = ((int32_t)s2->z) - ((int32_t)s1->z);
		total_delta += math_abs(delta);

		if (total_delta >= runtime_data.threshold_scaled) {
			/* Device has crossed the moving threshold */
			sch = task_schedule_from_data(runtime_data.task);
			infuse_state_set_timeout(INFUSE_STATE_DEVICE_MOVING,
						 sch->task_args.infuse.motion_id.in_motion_timeout);
			LOG_DBG("Movement Detected %d. Setting state for %d ticks", total_delta,
				sch->task_args.infuse.motion_id.in_motion_timeout);
			break;
		}
	}

process_end:
	/* Release IMU channel */
	zbus_chan_finish(ZBUS_CHAN);
	return rc;
}

/**
 * @brief Task main control work function.
 *
 * @param work
 */
void task_motion_id_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	int rc;

	/* Check for task termination */
	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT)) {
		/* Residual callback to terminate. Cancel zpoll callback if active */
		if (runtime_data.mode) {
			zbus_obs_set_enable(&imu_listener, false);
			runtime_data.mode = MOTION_ID_DISABLED;
		}
		return;
	}

	/* Identify the cause of wakeup */
	switch (runtime_data.mode) {
	case MOTION_ID_DISABLED:
		/* This is the first runtime. Initialise and wait on first sample of IMU data */
		runtime_data.task = task;
		runtime_data.mode = MOTION_ID_INITIALISING;
		/* Temporarily store in scaled threshold. This will be properly scaled during
		 * initialisation
		 */
		runtime_data.threshold_scaled = sch->task_args.infuse.motion_id.threshold_millig;

		/* Subscribe to IMU data as a listener */
		zbus_obs_set_enable(&imu_listener, true);
		/* Continue to initialisation */
	case MOTION_ID_INITIALISING:
		/* Check if there is any IMU metadata to try and complete initialisation */
		if (!zbus_chan_publish_count(ZBUS_CHAN)) {
			/* There is no data available initialise. Wait for data to come in */
			return;
		}

		/* Attempt to initialise based on data in the z_bus */
		rc = motion_id_initialise(false);

		if (rc < 0) {
			if (rc == -EBUSY) {
				/* New data is available, but initialisation failed because it was
				 * busy. Retry again soon.
				 */
				task_workqueue_reschedule(runtime_data.task, K_MSEC(10));
			}
			return;
		}
		/* Continue to running mode */
	case MOTION_ID_RUNNING:
		rc = motion_process();
		if (rc == -EBUSY) {
			task_workqueue_reschedule(runtime_data.task, K_MSEC(10));
		}
		return;
	default:
		LOG_WRN("Invalid operation state. Returning to disabled");
		runtime_data.mode = MOTION_ID_DISABLED;
		return;
	}
}

void imu_new_data_cb(const struct zbus_channel *chan)
{
	if (!runtime_data.mode) {
		/* Task is disabled, ignore operation */
		return;
	}

	if (runtime_data.mode == MOTION_ID_INITIALISING) {
		/* Initialise within callback since we already have possession of the data */
		motion_id_initialise(true);
		return;
	}

	/* Queue work item to run immediately */
	task_workqueue_reschedule(runtime_data.task, K_NO_WAIT);
}
