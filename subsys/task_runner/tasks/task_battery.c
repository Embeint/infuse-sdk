/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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

int task_battery_manual_run(const struct device *dev, const struct task_battery_args *args,
			    struct tdf_battery_state *tdf_battery)
{
	const uint32_t verbose_period_ms =
		CONFIG_TASK_RUNNER_TASK_BATTERY_VERBOSE_PRINT_PERIOD * MSEC_PER_SEC;
	static k_ticks_t next_verbose_print;
	union fuel_gauge_prop_val value;
	k_ticks_t now;
	int rc;

	/* Request fuel-gauge to be active */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "PM failure");
		return rc;
	}

	rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_VOLTAGE, &value);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "fetch failure");
		(void)pm_device_runtime_put(dev);
		return rc;
	}
	tdf_battery->voltage_mv = value.voltage / 1000;
	rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_CURRENT, &value);
	if (rc == 0) {
		tdf_battery->current_ua = value.current;
	} else if ((rc < 0) && (rc != -ENOTSUP)) {
		LOG_ERR("Charge current query failed (%d)", rc);
		tdf_battery->current_ua = -1;
	}
	rc = fuel_gauge_get_prop(dev, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &value);
	if (rc == 0) {
		tdf_battery->soc = value.relative_state_of_charge;
	} else if ((rc < 0) && (rc != -ENOTSUP)) {
		LOG_ERR("SoC query failed (%d)", rc);
		tdf_battery->soc = CONFIG_TASK_RUNNER_TASK_BATTERY_FALLBACK_SOC;
	}

	/* Release power requirement */
	rc = pm_device_runtime_put(dev);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, tdf_battery, K_FOREVER);

	/* Print the measured values */
	now = k_uptime_ticks();
	if (now >= next_verbose_print) {
		LOG_INF("%s: %6d mV (%3d %%) %6d uA", dev->name, tdf_battery->voltage_mv,
			tdf_battery->soc, tdf_battery->current_ua);
		next_verbose_print = now + k_ms_to_ticks_near32(verbose_period_ms);
	} else {
		LOG_DBG("%s: %6d mV (%3d %%) %6d uA", dev->name, tdf_battery->voltage_mv,
			tdf_battery->soc, tdf_battery->current_ua);
	}
	return 0;
}

void battery_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_battery_args *args = &sch->task_args.infuse.battery;
	const struct device *fuel_gauge = task->executor.workqueue.task_arg.const_arg;
	struct tdf_battery_state tdf_battery = {0};
	int rc;

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		LOG_DBG("Terminated by runner");
		return;
	}

	rc = task_battery_manual_run(fuel_gauge, &sch->task_args.infuse.battery, &tdf_battery);
	if (rc == 0) {
		/* Log output TDF */
		TASK_SCHEDULE_TDF_LOG(sch, TASK_BATTERY_LOG_COMPLETE, TDF_BATTERY_STATE,
				      epoch_time_now(), &tdf_battery);
	}

	if (args->repeat_interval_ms) {
		LOG_DBG("Rescheduling for %d ms", args->repeat_interval_ms);
		task_workqueue_reschedule(task, K_MSEC(args->repeat_interval_ms));
	}
}
