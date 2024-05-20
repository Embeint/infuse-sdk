/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <infuse/time/civil.h>
#include <infuse/tdf/definitions.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/imu.h>

#define IMU_SAMPLE_ARRAY_LEN 256
IMU_SAMPLE_ARRAY_CREATE(imu_sample_buffer, IMU_SAMPLE_ARRAY_LEN);

/* Validate IMU and TDFs match */
BUILD_ASSERT(sizeof(struct imu_sample) == sizeof(struct tdf_acc_4g));

LOG_MODULE_DECLARE(app);

static void imu_sample_handler(struct imu_sample_array *samples)
{
	struct imu_sample *last_acc, *last_gyr;
	uint64_t civil_time;

	/* Print last sample from each array */
	last_acc = &imu_sample_buffer->samples[imu_sample_buffer->accelerometer.offset +
					       imu_sample_buffer->accelerometer.num - 1];
	last_gyr = &imu_sample_buffer->samples[imu_sample_buffer->gyroscope.offset +
					       imu_sample_buffer->gyroscope.num - 1];
	if (imu_sample_buffer->accelerometer.num) {
		LOG_INF("ACC [%3d] %6d %6d %6d", imu_sample_buffer->accelerometer.num - 1,
			last_acc->x, last_acc->y, last_acc->z);
	}
	if (imu_sample_buffer->gyroscope.num) {
		LOG_INF("GYR [%3d] %6d %6d %6d", imu_sample_buffer->gyroscope.num - 1, last_gyr->x,
			last_gyr->y, last_gyr->z);
	}

	/* Log data as TDFs */
	if (imu_sample_buffer->accelerometer.num) {
		civil_time =
			civil_time_from_ticks(imu_sample_buffer->accelerometer.timestamp_ticks);
		tdf_data_logger_log_array(
			TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP, TDF_ACC_4G,
			sizeof(struct imu_sample), imu_sample_buffer->accelerometer.num, civil_time,
			civil_period_from_ticks(imu_sample_buffer->accelerometer.period_ticks),
			&imu_sample_buffer->samples[imu_sample_buffer->accelerometer.offset]);
	}
	if (imu_sample_buffer->gyroscope.num) {
		civil_time = civil_time_from_ticks(imu_sample_buffer->gyroscope.timestamp_ticks);
		tdf_data_logger_log_array(
			TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP, TDF_GYR_500DPS,
			sizeof(struct imu_sample), imu_sample_buffer->gyroscope.num, civil_time,
			civil_period_from_ticks(imu_sample_buffer->gyroscope.period_ticks),
			&imu_sample_buffer->samples[imu_sample_buffer->gyroscope.offset]);
	}
}

static int imu_sampler(void *a, void *b, void *c)
{
	const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(bmi270));
	struct imu_config config = {
		.accelerometer =
			{
				.full_scale_range = 4,
				.sample_rate_hz = 50,
				.low_power = false,
			},
		.gyroscope =
			{
				.full_scale_range = 500,
				.sample_rate_hz = 50,
				.low_power = false,
			},
		.fifo_sample_buffer = 100,
	};
	struct imu_config_output config_output;
	k_timeout_t interrupt_timeout;
	int rc;

	rc = imu_configure(imu, &config, &config_output);

	LOG_WRN("%d Acc period: %d us Gyr period: %d us Int period: %d us", rc,
		config_output.accelerometer_period_us, config_output.gyroscope_period_us,
		config_output.expected_interrupt_period_us);

	interrupt_timeout = K_USEC(2 * config_output.expected_interrupt_period_us);
	while (true) {
		/* Wait for the next IMU interrupt */
		rc = imu_data_wait(imu, interrupt_timeout);
		if (rc < 0) {
			LOG_ERR("Timed out waiting for data");
			break;
		}

		/* Read IMU samples */
		rc = imu_data_read(imu, imu_sample_buffer, IMU_SAMPLE_ARRAY_LEN);
		if (rc < 0) {
			LOG_ERR("Failed to read IMU samples (%d)", rc);
			break;
		}

		/* Handle the samples */
		imu_sample_handler(imu_sample_buffer);
	}

	/* Put IMU back into low power mode */
	(void)imu_configure(imu, NULL, NULL);
	k_sleep(K_FOREVER);
	return 0;
}

K_THREAD_DEFINE(imu_sampler_thread, 2048, imu_sampler, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
