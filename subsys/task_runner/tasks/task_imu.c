/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/imu.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/drivers/imu.h>
#include <infuse/zbus/channels.h>

IMU_SAMPLE_ARRAY_TYPE_DEFINE(task_imu_sample_container, CONFIG_TASK_RUNNER_TASK_IMU_MAX_FIFO);

ZBUS_CHAN_ID_DEFINE(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_IMU), INFUSE_ZBUS_CHAN_IMU,
		    struct task_imu_sample_container, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		    ZBUS_MSG_INIT(0));
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

LOG_MODULE_REGISTER(task_imu, CONFIG_TASK_IMU_LOG_LEVEL);

struct logging_state {
	uint16_t acc_tdf;
	uint16_t gyr_tdf;
	uint16_t mag_tdf;
};

static void imu_log_config_init(const struct task_imu_args *args, struct logging_state *log_state)
{

	switch (args->accelerometer.range_g) {
	case 2:
		log_state->acc_tdf = TDF_ACC_2G;
		break;
	case 4:
		log_state->acc_tdf = TDF_ACC_4G;
		break;
	case 8:
		log_state->acc_tdf = TDF_ACC_8G;
		break;
	default:
		log_state->acc_tdf = TDF_ACC_16G;
		break;
	}
	switch (args->gyroscope.range_dps) {
	case 125:
		log_state->gyr_tdf = TDF_GYR_125DPS;
		break;
	case 250:
		log_state->gyr_tdf = TDF_GYR_250DPS;
		break;
	case 500:
		log_state->gyr_tdf = TDF_GYR_500DPS;
		break;
	case 1000:
		log_state->gyr_tdf = TDF_GYR_1000DPS;
		break;
	default:
		log_state->gyr_tdf = TDF_GYR_2000DPS;
		break;
	}
}

static void imu_sample_handler(const struct task_schedule *schedule,
			       const struct logging_state *log_state,
			       const struct imu_sample_array *samples)
{
	const struct imu_sample *last_acc, *last_gyr;
	uint64_t epoch_time;

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
		epoch_time = epoch_time_from_ticks(samples->accelerometer.timestamp_ticks);

		task_schedule_tdf_log_array(
			schedule, TASK_IMU_LOG_ACC, log_state->acc_tdf, sizeof(struct imu_sample),
			samples->accelerometer.num, epoch_time,
			epoch_period_from_ticks(samples->accelerometer.period_ticks),
			&samples->samples[samples->accelerometer.offset]);
	}
	if (samples->gyroscope.num) {
		epoch_time = epoch_time_from_ticks(samples->gyroscope.timestamp_ticks);

		task_schedule_tdf_log_array(
			schedule, TASK_IMU_LOG_GYR, log_state->gyr_tdf, sizeof(struct imu_sample),
			samples->gyroscope.num, epoch_time,
			epoch_period_from_ticks(samples->gyroscope.period_ticks),
			&samples->samples[samples->gyroscope.offset]);
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

	LOG_WRN("%d Acc period: %d us Gyr period: %d us Int period: %d us", rc,
		config_output.accelerometer_period_us, config_output.gyroscope_period_us,
		config_output.expected_interrupt_period_us);

	imu_log_config_init(args, &log_state);
	interrupt_timeout = K_USEC(2 * config_output.expected_interrupt_period_us);

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
		zbus_chan_update_publish_metadata(ZBUS_CHAN);
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
}
