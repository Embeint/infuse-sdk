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

static int env_sampler(void *a, void *b, void *c)
{
	const struct device *env = DEVICE_DT_GET(DT_NODELABEL(bme688));
	struct tdf_environmental tdf_env;
	struct sensor_value value;
	int rc;

	uint64_t now = k_uptime_get();

	while (true) {
		now += 5000;
		/* Wait until the next sample time */
		k_sleep(K_TIMEOUT_ABS_MS(now));

		/* Trigger the sample */
		rc = sensor_sample_fetch(env);
		if (rc < 0) {
			LOG_ERR("Failed to fetch %s (%d)", env->name, rc);
			break;
		}

		/* Populate the output TDF */
		rc = sensor_channel_get(env, SENSOR_CHAN_AMBIENT_TEMP, &value);
		tdf_env.temperature = sensor_value_to_milli(&value);
		rc = sensor_channel_get(env, SENSOR_CHAN_PRESS, &value);
		tdf_env.pressure = sensor_value_to_milli(&value);
		rc = sensor_channel_get(env, SENSOR_CHAN_HUMIDITY, &value);
		tdf_env.humidity = sensor_value_to_centi(&value);

		/* Push the output TDF over serial */
		tdf_data_logger_log(TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP, TDF_ENVIRONMENTAL, sizeof(tdf_env),
				    civil_time_now(), &tdf_env);

		/* Print the measured values */
		LOG_INF("Sensor: %s", env->name);
		LOG_INF("\tTemperature: %6d mDeg", tdf_env.temperature);
		LOG_INF("\t   Pressure: %6d Pa", tdf_env.pressure);
		LOG_INF("\t   Humidity: %6d %%", tdf_env.humidity / 100);
	}
	k_sleep(K_FOREVER);
	return 0;
}

K_THREAD_DEFINE(env_sampler_thread, 2048, env_sampler, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
