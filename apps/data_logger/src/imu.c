/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
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

LOG_MODULE_DECLARE(app);

static int imu_sampler(void *a, void *b, void *c)
{
	const struct device *imu = DEVICE_DT_GET(DT_NODELABEL(bmi270));
	struct tdf_acc_2g tdf_acc[64];
	struct sensor_value value;
	int sample_cnt = 0;
	int rc;

	value.val1 = 50;
	value.val2 = 0;
	rc = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &value);

	value.val1 = 4;
	value.val2 = 0;
	rc = sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &value);

	uint64_t now = k_uptime_get();

	while (true) {
		now += 100;
		/* Wait until the next sample time */
		k_sleep(K_TIMEOUT_ABS_MS(now));

		/* Read the latest value */
		rc = sensor_sample_fetch(imu);
		if (rc < 0) {
			LOG_ERR("Failed to fetch %s (%d)", imu->name, rc);
			break;
		}

		/* Push into the TDF array */
		rc = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_X, &value);
		tdf_acc[sample_cnt].sample.x = sensor_value_to_milli(&value);
		rc = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_Y, &value);
		tdf_acc[sample_cnt].sample.y = sensor_value_to_milli(&value);
		rc = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_Z, &value);
		tdf_acc[sample_cnt].sample.z = sensor_value_to_milli(&value);
		sample_cnt++;

		/* Buffer not filled */
		if (sample_cnt != ARRAY_SIZE(tdf_acc)) {
			continue;
		}
		sample_cnt = 0;

		tdf_data_logger_log_array(TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP, TDF_ACC_4G, sizeof(tdf_acc[0]),
					  ARRAY_SIZE(tdf_acc), civil_time_now(), 65536 / 10, tdf_acc);

		/* Print the measured values */
		LOG_INF("Sensor: %s", imu->name);
		LOG_INF("\tX: %6d", tdf_acc[15].sample.x);
		LOG_INF("\tY: %6d", tdf_acc[15].sample.y);
		LOG_INF("\tZ: %6d", tdf_acc[15].sample.z);
	}
	k_sleep(K_FOREVER);
	return 0;
}

K_THREAD_DEFINE(imu_sampler_thread, 2048, imu_sampler, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
