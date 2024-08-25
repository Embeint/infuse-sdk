/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/battery.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

LOG_MODULE_REGISTER(task_bat, CONFIG_TASK_BATTERY_LOG_LEVEL);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

void battery_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct device *fuel_gauge = task->executor.workqueue.task_arg.const_arg;
	struct tdf_battery_state tdf_battery = {0};
	union fuel_gauge_prop_val value;
	int rc;

	/* Request fuel-gauge to be active */
	rc = pm_device_runtime_get(fuel_gauge);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	rc = fuel_gauge_get_prop(fuel_gauge, FUEL_GAUGE_VOLTAGE, &value);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "fetch failure");
		return;
	}
	tdf_battery.voltage_mv = value.voltage / 1000;
	rc = fuel_gauge_get_prop(fuel_gauge, FUEL_GAUGE_CURRENT, &value);
	if (rc == 0) {
		/* Negative values from fuel-gauge indicate discharging */
		tdf_battery.charge_ua = MAX(0, value.current);
	} else if ((rc < 0) && (rc != -ENOTSUP)) {
		LOG_ERR("Charge current query failed (%d)", rc);
	}
	rc = fuel_gauge_get_prop(fuel_gauge, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &value);
	if (rc == 0) {
		tdf_battery.soc = 100 * (uint16_t)value.relative_state_of_charge;
	} else if ((rc < 0) && (rc != -ENOTSUP)) {
		LOG_ERR("SoC query failed (%d)", rc);
	}

	/* Release power requirement */
	rc = pm_device_runtime_put(fuel_gauge);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Log output TDF */
	task_schedule_tdf_log(sch, TASK_BATTERY_LOG_COMPLETE, TDF_BATTERY_STATE,
			      sizeof(tdf_battery), epoch_time_now(), &tdf_battery);

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &tdf_battery, K_FOREVER);

	/* Print the measured values */
	LOG_INF("Sensor: %s", fuel_gauge->name);
	LOG_INF("\t        Voltage: %6d mV", tdf_battery.voltage_mv);
	LOG_INF("\tState-of-charge: %6d %%", tdf_battery.soc / 100);
	LOG_INF("\t Charge Current: %6d uA", tdf_battery.charge_ua);
}
