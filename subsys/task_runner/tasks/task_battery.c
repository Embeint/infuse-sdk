/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/battery.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/civil.h>
#include <infuse/zbus/channels.h>

LOG_MODULE_REGISTER(task_bat, CONFIG_TASK_BATTERY_LOG_LEVEL);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

void battery_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct device *battery = task->executor.workqueue.task_arg.const_arg;
	struct tdf_battery_state tdf_battery;
	struct sensor_value value;
	int rc;

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(battery);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	/* Trigger the sample */
	rc = sensor_sample_fetch(battery);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "fetch failure");
		return;
	}

	/* Populate the output TDF */
	(void)sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	tdf_battery.voltage_mv = sensor_value_to_milli(&value);
	(void)sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &value);
	tdf_battery.soc = sensor_value_to_centi(&value);
	tdf_battery.charge_ua = 0;

	/* Release power requirement */
	rc = pm_device_runtime_put(battery);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Log output TDF */
	task_schedule_tdf_log(sch, TASK_BATTERY_LOG_COMPLETE, TDF_BATTERY_STATE,
			      sizeof(tdf_battery), civil_time_now(), &tdf_battery);

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &tdf_battery, K_FOREVER);

	/* Print the measured values */
	LOG_INF("Sensor: %s", battery->name);
	LOG_INF("\t        Voltage: %6d mV", tdf_battery.voltage_mv);
	LOG_INF("\tState-of-charge: %6d %%", tdf_battery.soc / 100);
	LOG_INF("\t Charge Current: %6d uA", tdf_battery.charge_ua);
}
