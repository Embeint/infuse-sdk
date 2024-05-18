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

static int bat_sampler(void *a, void *b, void *c)
{
	const struct device *bat = DEVICE_DT_GET(DT_NODELABEL(vbatt));
	struct tdf_battery_state tdf_battery;
	struct sensor_value value;
	int rc;

	uint64_t now = k_uptime_get();

	while (true) {
		now += 5000;
		/* Wait until the next sample time */
		k_sleep(K_TIMEOUT_ABS_MS(now));

		/* Trigger the sample */
		rc = sensor_sample_fetch(bat);
		if (rc < 0) {
			LOG_ERR("Failed to fetch %s (%d)", bat->name, rc);
			break;
		}

		/* Populate the output TDF */
		(void)sensor_channel_get(bat, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
		tdf_battery.voltage_mv = sensor_value_to_milli(&value);
		(void)sensor_channel_get(bat, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &value);
		tdf_battery.soc = sensor_value_to_centi(&value);
		tdf_battery.charge_ua = 0;

		/* Push the output TDF over serial */
		tdf_data_logger_log(TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_UDP, TDF_BATTERY_STATE,
				    sizeof(tdf_battery), civil_time_now(), &tdf_battery);

		/* Print the measured values */
		LOG_INF("Sensor: %s", bat->name);
		LOG_INF("\t        Voltage: %6d mV", tdf_battery.voltage_mv);
		LOG_INF("\tState-of-charge: %6d %%", tdf_battery.soc / 100);
		LOG_INF("\t Charge Current: %6d uA", tdf_battery.charge_ua);
	}
	k_sleep(K_FOREVER);
	return 0;
}

K_THREAD_DEFINE(bat_sampler_thread, 2048, bat_sampler, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
