/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/drivers/imu.h>
#include <infuse/drivers/imu/data_types.h>
#include <infuse/math/common.h>
#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/imu.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

IMU_SAMPLE_ARRAY_TYPE_DEFINE(task_imu_sample_container, CONFIG_TASK_RUNNER_TASK_IMU_MAX_FIFO);

ZBUS_CHAN_DEFINE_WITH_ID(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), INFUSE_ZBUS_CHAN_IMU,
			 struct task_imu_sample_container, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
			 ZBUS_MSG_INIT(0));
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

#ifdef CONFIG_TASK_RUNNER_TASK_IMU_ACC_MAGNITUDE_BROADCAST

IMU_MAG_ARRAY_TYPE_DEFINE(task_imu_acc_mag_container,
			  CONFIG_TASK_RUNNER_TASK_IMU_ACC_MAGNITUDE_BROADCAST_MAX);

ZBUS_CHAN_DEFINE_WITH_ID(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU_ACC_MAG),
			 INFUSE_ZBUS_CHAN_IMU_ACC_MAG, struct task_imu_acc_mag_container, NULL,
			 NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
#define ZBUS_CHAN_MAG INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU_ACC_MAG)

#endif /* CONFIG_TASK_RUNNER_TASK_IMU_ACC_MAGNITUDE_BROADCAST */

#if defined(CONFIG_TASK_RUNNER_TASK_IMU_LOG_TIME_ARRAY)
static const enum tdf_data_format tdf_format = TDF_DATA_FORMAT_TIME_ARRAY;
#elif defined(CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY)
static const enum tdf_data_format tdf_format = TDF_DATA_FORMAT_IDX_ARRAY;
#elif defined(CONFIG_TASK_RUNNER_TASK_IMU_LOG_DIFF_ARRAY)
static const enum tdf_data_format tdf_format = TDF_DATA_FORMAT_DIFF_ARRAY_16_8;
#else
#error Unhandled logging data type
#endif

LOG_MODULE_REGISTER(task_imu, CONFIG_TASK_IMU_LOG_LEVEL);

struct logging_state {
	uint16_t acc_tdf;
	uint16_t gyr_tdf;
#ifdef CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY
	uint32_t acc_period;
	uint32_t gyr_period;
	uint32_t acc_idx;
	uint32_t gyr_idx;
#endif
};

static void imu_sample_handler(const struct task_schedule *schedule,
			       struct logging_state *log_state,
			       const struct imu_sample_array *samples)
{
	const struct imu_sample *last_acc, *last_gyr;
	uint64_t epoch_time;

#ifdef CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY
	struct tdf_idx_array_period idx_meta;
#endif /* CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY */

#ifdef CONFIG_TASK_RUNNER_TASK_IMU_ACC_MAGNITUDE_BROADCAST
	struct task_imu_acc_mag_container *mags;
	const struct imu_sample *s = &samples->samples[samples->accelerometer.offset];
	uint16_t num = MIN(samples->accelerometer.num, ARRAY_SIZE(mags->magnitudes));

	/* Claim the channel so we can process directly into the memory buffer */
	zbus_chan_claim(ZBUS_CHAN_MAG, K_FOREVER);
	mags = ZBUS_CHAN_MAG->message;

	mags->meta = samples->accelerometer;
	mags->meta.offset = 0;
	mags->meta.num = num;

	/* Calculate magnitudes */
	for (int i = 0; i < num; i++) {
		mags->magnitudes[i] = math_vector_xyz_magnitude(s->x, s->y, s->z);
		s++;
	}

	/* Update metadata, finish claim, notify subscribers */
	zbus_chan_pub_stats_update(ZBUS_CHAN_MAG);
	zbus_chan_finish(ZBUS_CHAN_MAG);
	zbus_chan_notify(ZBUS_CHAN_MAG, K_FOREVER);
#endif /* CONFIG_TASK_RUNNER_TASK_IMU_ACC_MAGNITUDE_BROADCAST */

	/* Print last sample from each array */
	last_acc =
		&samples->samples[samples->accelerometer.offset + samples->accelerometer.num - 1];
	last_gyr = &samples->samples[samples->gyroscope.offset + samples->gyroscope.num - 1];
	if (samples->accelerometer.num) {
		LOG_DBG("ACC [%3d] %6d %6d %6d", samples->accelerometer.num - 1, last_acc->x,
			last_acc->y, last_acc->z);
	}
	if (samples->gyroscope.num) {
		LOG_DBG("GYR [%3d] %6d %6d %6d", samples->gyroscope.num - 1, last_gyr->x,
			last_gyr->y, last_gyr->z);
	}

	/* Log data as TDFs */
	if (samples->accelerometer.num) {
#ifdef CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY
		epoch_time = 0;
		if (log_state->acc_period) {
			epoch_time = epoch_time_from_ticks(samples->accelerometer.timestamp_ticks);

			/* Log timing metadata */
			idx_meta.tdf_id = log_state->acc_tdf;
			idx_meta.period = log_state->acc_period * 1000;
			task_schedule_tdf_log(schedule, TASK_IMU_LOG_ACC, TDF_IDX_ARRAY_PERIOD,
					      sizeof(idx_meta), epoch_time, &idx_meta);
			log_state->acc_period = 0;
		}

		task_schedule_tdf_log_core(schedule, TASK_IMU_LOG_ACC, log_state->acc_tdf,
					   sizeof(struct imu_sample), samples->accelerometer.num,
					   tdf_format, epoch_time, log_state->acc_idx,
					   &samples->samples[samples->accelerometer.offset]);

		log_state->acc_idx += samples->accelerometer.num;
#else
		epoch_time = epoch_time_from_ticks(samples->accelerometer.timestamp_ticks);

		task_schedule_tdf_log_core(
			schedule, TASK_IMU_LOG_ACC, log_state->acc_tdf, sizeof(struct imu_sample),
			samples->accelerometer.num, tdf_format, epoch_time,
			epoch_period_from_array_ticks(samples->accelerometer.buffer_period_ticks,
						      samples->accelerometer.num),
			&samples->samples[samples->accelerometer.offset]);
#endif /* CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY */
	}
	if (samples->gyroscope.num) {
#ifdef CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY
		epoch_time = 0;
		if (log_state->gyr_period) {
			epoch_time = epoch_time_from_ticks(samples->gyroscope.timestamp_ticks);

			/* Log timing metadata */
			idx_meta.tdf_id = log_state->gyr_tdf;
			idx_meta.period = log_state->gyr_period * 1000;
			task_schedule_tdf_log(schedule, TASK_IMU_LOG_GYR, TDF_IDX_ARRAY_PERIOD,
					      sizeof(idx_meta), epoch_time, &idx_meta);
			log_state->gyr_period = 0;
		}

		task_schedule_tdf_log_core(schedule, TASK_IMU_LOG_GYR, log_state->gyr_tdf,
					   sizeof(struct imu_sample), samples->gyroscope.num,
					   tdf_format, epoch_time, log_state->gyr_idx,
					   &samples->samples[samples->gyroscope.offset]);

		log_state->gyr_idx += samples->gyroscope.num;
#else
		epoch_time = epoch_time_from_ticks(samples->gyroscope.timestamp_ticks);

		task_schedule_tdf_log_core(
			schedule, TASK_IMU_LOG_GYR, log_state->gyr_tdf, sizeof(struct imu_sample),
			samples->gyroscope.num, tdf_format, epoch_time,
			epoch_period_from_array_ticks(samples->gyroscope.buffer_period_ticks,
						      samples->gyroscope.num),
			&samples->samples[samples->gyroscope.offset]);
#endif /* CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY */
	}
}

void imu_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
		 void *imu_dev)
{
	const struct device *imu = imu_dev;
	const struct task_imu_args *args = &schedule->task_args.infuse.imu;
	bool low_power_mode = args->flags & TASK_IMU_FLAGS_LOW_POWER_MODE;
	struct imu_config config = {
		.accelerometer =
			{
				.full_scale_range = args->accelerometer.range_g,
				.sample_rate_hz = args->accelerometer.rate_hz,
				.low_power = low_power_mode,
			},
		.gyroscope =
			{
				.full_scale_range = args->gyroscope.range_dps,
				.sample_rate_hz = args->gyroscope.rate_hz,
				.low_power = low_power_mode,
			},
		.fifo_sample_buffer =
			MIN(args->fifo_sample_buffer, CONFIG_TASK_RUNNER_TASK_IMU_MAX_FIFO),
	};
	struct imu_config_output config_output;
	struct logging_state log_state = {0};
	k_timeout_t interrupt_timeout;
	uint32_t buffer_count = 0;
	int rc;

	LOG_DBG("Starting");

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(imu);
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	/* Configure IMU */
	rc = imu_configure(imu, &config, &config_output);
	if (rc < 0) {
		k_sleep(K_SECONDS(1));
		LOG_ERR("Terminating due to %s", "configuration failure");
		return;
	}

	LOG_INF("Acc period: %d us Gyr period: %d us Int period: %d us",
		config_output.accelerometer_period_us, config_output.gyroscope_period_us,
		config_output.expected_interrupt_period_us);

	log_state.acc_tdf = tdf_id_from_accelerometer_range(args->accelerometer.range_g);
	log_state.gyr_tdf = tdf_id_from_gyroscope_range(args->gyroscope.range_dps);
	interrupt_timeout = K_USEC(2 * config_output.expected_interrupt_period_us);

#ifdef CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY
	log_state.acc_period = config_output.accelerometer_period_us;
	log_state.gyr_period = config_output.gyroscope_period_us;
	log_state.acc_idx = 0;
	log_state.gyr_idx = 0;
#endif /* CONFIG_TASK_RUNNER_TASK_IMU_LOG_IDX_ARRAY */

	while (true) {
		/* Wait for the next IMU interrupt */
		rc = imu_data_wait(imu, interrupt_timeout);
		if (rc < 0) {
			LOG_ERR("Terminating due to %s", "interrupt timeout");
			break;
		}

		/* Claim the channel so we can read directly into the memory buffer */
		zbus_chan_claim(ZBUS_CHAN, K_FOREVER);

		/* Read IMU samples */
		rc = imu_data_read(imu, ZBUS_CHAN->message, CONFIG_TASK_RUNNER_TASK_IMU_MAX_FIFO);
		if (rc < 0) {
			LOG_ERR("Terminating due to %s", "data read failure");
			zbus_chan_finish(ZBUS_CHAN);
			break;
		}

		/* Handle the samples */
		imu_sample_handler(schedule, &log_state, ZBUS_CHAN->message);

		/* Update metadata, finish claim, notify subscribers */
		zbus_chan_pub_stats_update(ZBUS_CHAN);
		zbus_chan_finish(ZBUS_CHAN);
		zbus_chan_notify(ZBUS_CHAN, K_FOREVER);

		/* Check for termination conditions */
		buffer_count++;
		if ((args->num_buffers > 0) && (buffer_count == args->num_buffers)) {
			LOG_INF("Terminating due to %s", "buffer count");
			break;
		}
		if (task_runner_task_block(terminate, K_NO_WAIT) == 1) {
			LOG_INF("Terminating due to %s", "runner request");
			break;
		}
	}

	/* Put IMU back into low power mode */
	(void)imu_configure(imu, NULL, NULL);

	/* Release power requirement */
	rc = pm_device_runtime_put(imu);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Terminate thread */
	LOG_DBG("Terminating");
}
