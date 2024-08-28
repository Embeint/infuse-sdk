/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/pm/device_runtime.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include <infuse/drivers/imu.h>
#include <infuse/validation/core.h>
#include <infuse/validation/imu.h>
#include <infuse/math/common.h>

#define TEST        "IMU"
#define MAX_SAMPLES 256
IMU_SAMPLE_ARRAY_CREATE(imu_samples, MAX_SAMPLES);

static int validate_sample_timing(const struct device *dev, uint8_t acc_range,
				  uint16_t acc_sample_rate, uint16_t gyr_sample_rate)
{
	struct imu_config config = {
		.accelerometer =
			{
				.full_scale_range = acc_range,
				.sample_rate_hz = acc_sample_rate,
				.low_power = false,
			},
		.gyroscope =
			{
				.full_scale_range = 500,
				.sample_rate_hz = gyr_sample_rate,
				.low_power = false,
			},
		.fifo_sample_buffer = (acc_sample_rate + gyr_sample_rate) / 2,
	};
	struct imu_config_output config_output;
	int64_t wait_start, wait_end, wait_us;
	k_timeout_t interrupt_timeout;
	int64_t previous_timestamp_acc = 0, previous_timestamp_gyr = 0;
	uint32_t read_delay_ms;
	uint16_t vector_mag;
	int rc, rc2;

	/* Configure IMU */
	rc = imu_configure(dev, &config, &config_output);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to configure (%d)", rc);
		return rc;
	}
	wait_start = k_uptime_ticks();

	/* Expect samples to be within 10% of 1G */
	int32_t one_g = imu_accelerometer_1g(config.accelerometer.full_scale_range);
	uint16_t one_g_min = (90 * one_g) / 100;
	uint16_t one_g_max = (110 * one_g) / 100;

	/* Expect interrupt period to be within 20% of that reported by the config function */
	int64_t int_expected = config_output.expected_interrupt_period_us;
	int64_t int_threshold_min = (80 * int_expected) / 100;
	int64_t int_threshold_max = (120 * int_expected) / 100;

	/* Expect sample periods to be within 10% of that reported by the config function */
	uint32_t acc_expected = k_us_to_ticks_near32(config_output.accelerometer_period_us);
	uint32_t acc_threshold_min = (10 * acc_expected) / 100;
	uint32_t acc_threshold_max = (110 * acc_expected) / 100;
	uint32_t gyr_expected = k_us_to_ticks_near32(config_output.gyroscope_period_us);
	uint32_t gyr_threshold_min = (10 * gyr_expected) / 100;
	uint32_t gyr_threshold_max = (110 * gyr_expected) / 100;

	interrupt_timeout = K_USEC(2 * int_expected);

	/* Run for 5 sample buffers */
	for (int i = 0; i < 5; i++) {
		/* Wait for the interrupt */
		rc = imu_data_wait(dev, interrupt_timeout);
		wait_end = k_uptime_ticks();
		wait_us = k_ticks_to_us_near64(wait_end - wait_start);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Interrupt timeout");
			rc = -EINVAL;
			break;
		}

		/* Delay for a random time period before reading samples to ensure that
		 * the driver generates correct timestamps when more samples are added to
		 * the FIFO after the interrupt.
		 */
		read_delay_ms = sys_rand32_get() % 100;
		k_sleep(K_MSEC(read_delay_ms));

		/* The time we query the FIFO for pending data is in practice when the
		 * clock starts for the next interrupt.
		 */
		wait_start = k_uptime_ticks();

		/* Read IMU samples */
		rc = imu_data_read(dev, imu_samples, MAX_SAMPLES);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "Data read failed (%d)", rc);
			rc = -EINVAL;
			break;
		}

		/* Validate reported ranges */
		if (config.accelerometer.sample_rate_hz) {
			if (config.accelerometer.full_scale_range !=
			    imu_samples->accelerometer.full_scale_range) {
				VALIDATION_REPORT_ERROR(
					TEST, "Acc range mismatch (%u != %u)",
					config.accelerometer.full_scale_range,
					imu_samples->accelerometer.full_scale_range);
				rc = -EINVAL;
				break;
			}
		}
		if (config.gyroscope.sample_rate_hz) {
			if (config.gyroscope.full_scale_range !=
			    imu_samples->gyroscope.full_scale_range) {
				VALIDATION_REPORT_ERROR(TEST, "Gyro range mismatch (%u != %u)",
							config.gyroscope.full_scale_range,
							imu_samples->gyroscope.full_scale_range);
				rc = -EINVAL;
				break;
			}
		}

		/* Check timestamps across buffers */
		if (acc_sample_rate && previous_timestamp_acc) {
			int64_t diff =
				imu_samples->accelerometer.timestamp_ticks - previous_timestamp_acc;

			if ((diff < acc_threshold_min) || (diff > acc_threshold_max)) {
				VALIDATION_REPORT_ERROR(
					TEST, "Acc inter-buffer period (%lld too far from %u)",
					diff, acc_expected);
				rc = -EINVAL;
				break;
			}
		}
		if (gyr_sample_rate && previous_timestamp_gyr) {
			int64_t diff =
				imu_samples->gyroscope.timestamp_ticks - previous_timestamp_gyr;

			if ((diff < gyr_threshold_min) || (diff > gyr_threshold_max)) {
				VALIDATION_REPORT_ERROR(
					TEST, "Gyro inter-buffer period (%lld too far from %u)",
					diff, gyr_expected);
				rc = -EINVAL;
				break;
			}
		}

		/* Update timestamp of last seen samples */
		previous_timestamp_acc = imu_samples->accelerometer.timestamp_ticks +
					 imu_samples->accelerometer.buffer_period_ticks;
		previous_timestamp_gyr = imu_samples->gyroscope.timestamp_ticks +
					 imu_samples->gyroscope.buffer_period_ticks;

		/* First buffer after boot usually contains startup transients as filters start */
		if (i == 0) {
			continue;
		}

		/* Compare actual interrupt period with the expected period */
		{
			if ((wait_us < int_threshold_min) || (wait_us > int_threshold_max)) {
				VALIDATION_REPORT_ERROR(TEST,
							"Interrupt period (%lld too far from %lld)",
							wait_us, int_expected);
				rc = -EINVAL;
				break;
			}
		}

		/* Check reported periods */
		if (acc_sample_rate > 0) {
			if (imu_samples->accelerometer.num == 0) {
				VALIDATION_REPORT_ERROR(TEST, "Acc reported no samples");
				rc = -EINVAL;
				break;
			}

			uint32_t acc_sample_period_ticks =
				imu_samples->accelerometer.buffer_period_ticks /
				imu_samples->accelerometer.num;

			if ((acc_sample_period_ticks < acc_threshold_min) ||
			    (acc_sample_period_ticks > acc_threshold_max)) {
				VALIDATION_REPORT_ERROR(TEST,
							"Acc reported period (%u too far from %u)",
							acc_sample_period_ticks, acc_expected);
				rc = -EINVAL;
				break;
			}
		}
		if (gyr_sample_rate > 0) {
			if (imu_samples->gyroscope.num == 0) {
				VALIDATION_REPORT_ERROR(TEST, "Gyro reported no samples");
				rc = -EINVAL;
				break;
			}

			uint32_t gyr_sample_period_ticks =
				imu_samples->gyroscope.buffer_period_ticks /
				imu_samples->gyroscope.num;

			if ((gyr_sample_period_ticks < gyr_threshold_min) ||
			    (gyr_sample_period_ticks > gyr_threshold_max)) {
				VALIDATION_REPORT_ERROR(TEST,
							"Gyro reported period (%u too far from %u)",
							gyr_sample_period_ticks, gyr_expected);
				rc = -EINVAL;
				break;
			}
		}

		/* Validate vector magnitude */
		for (int j = 0; j < imu_samples->accelerometer.num; j++) {
			struct imu_sample *s =
				&imu_samples->samples[imu_samples->accelerometer.offset + j];

			vector_mag = math_vector_xyz_magnitude(s->x, s->y, s->z);
			if ((vector_mag < one_g_min) || (vector_mag > one_g_max)) {
				VALIDATION_REPORT_ERROR(
					TEST, "Accelerometer magnitude out of range [%d](%d)", j,
					vector_mag);
				rc = -EINVAL;
				goto loop_break;
			}
		}
	}
loop_break:

	/* Reset IMU */
	rc2 = imu_configure(dev, NULL, NULL);
	if (rc2 < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to reset (%d)", rc2);
		return rc2;
	}
	return rc == 0 ? rc2 : rc;
}

int infuse_validation_imu(const struct device *dev, uint8_t flags)
{
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		rc = -ENODEV;
		goto test_end;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	if (flags & VALIDATION_IMU_DRIVER) {
		VALIDATION_REPORT_INFO(TEST, "Driver test @ (Acc 2G 50Hz) (Gyr N/A)");
		rc = validate_sample_timing(dev, 2, 50, 0);
		if (rc < 0) {
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "Driver test @ (Acc N/A) (Gyr 50Hz)");
		rc = validate_sample_timing(dev, 8, 0, 50);
		if (rc < 0) {
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "Driver test @ (Acc 4G 50Hz) (Gyr 25Hz)");
		rc = validate_sample_timing(dev, 4, 50, 25);
		if (rc < 0) {
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "Driver test @ (Acc 2G 25Hz) (Gyr 50Hz)");
		rc = validate_sample_timing(dev, 2, 25, 50);
		if (rc < 0) {
			goto driver_end;
		}
		VALIDATION_REPORT_INFO(TEST, "Driver test @ (Acc 8G 100Hz) (Gyr 100Hz)");
		rc = validate_sample_timing(dev, 8, 100, 100);
		if (rc < 0) {
			goto driver_end;
		}
	}
driver_end:

	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
