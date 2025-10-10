/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/environmental.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

LOG_MODULE_REGISTER(task_env, CONFIG_TASK_ENVIRONMENTAL_LOG_LEVEL);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)

static int env_fetch(const struct device *dev)
{
	int rc;

	/* Validate existence and init state */
	if (dev == NULL) {
		return -ENODEV;
	}
	if (!device_is_ready(dev)) {
		LOG_WRN_ONCE("%s not ready", dev->name);
		return -ENODEV;
	}
	/* Request sensor to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		return rc;
	}

	/* Trigger the sample */
	rc = sensor_sample_fetch(dev);
	if (rc < 0) {
		/* Release PM constraint */
		(void)pm_device_runtime_put(dev);
		return rc;
	}
	return 0;
}

static void env_release(const struct device *dev, bool sampled)
{
	int rc;

	if (!sampled) {
		return;
	}
	rc = pm_device_runtime_put(dev);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}
}

void environmental_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct task_environmental_devices *devices =
		task->executor.workqueue.task_arg.const_arg;
	const struct device *primary = devices->primary;
	const struct device *secondary = devices->secondary;
	struct tdf_ambient_temp_pres_hum tdf_tph = {0};
	struct tdf_ambient_temperature tdf_temp = {0};
	struct sensor_value value;
	bool primary_sampled = false;
	bool secondary_sampled = false;
	bool has_pressure = false;
	bool has_humidity = false;
	int rc;

	/* Sample from provided sensors */
	rc = env_fetch(primary);
	if (rc == 0) {
		primary_sampled = true;
	}
	rc = env_fetch(secondary);
	if (rc == 0) {
		secondary_sampled = true;
	}

	if (!primary_sampled && !secondary_sampled) {
		LOG_ERR("Terminating due to no samples");
		return;
	}

	LOG_DBG("Sources: %d %d", primary_sampled, secondary_sampled);

	/* Populate the output TDFs */
	if ((primary_sampled &&
	     (sensor_channel_get(primary, SENSOR_CHAN_AMBIENT_TEMP, &value) == 0)) ||
	    ((secondary_sampled &&
	      (sensor_channel_get(secondary, SENSOR_CHAN_AMBIENT_TEMP, &value) == 0)))) {
		tdf_tph.temperature = sensor_value_to_milli(&value);
		tdf_temp.temperature = tdf_tph.temperature;
	}

	if ((primary_sampled && (sensor_channel_get(primary, SENSOR_CHAN_PRESS, &value) == 0)) ||
	    ((secondary_sampled &&
	      (sensor_channel_get(secondary, SENSOR_CHAN_PRESS, &value) == 0)))) {
		tdf_tph.pressure = sensor_value_to_milli(&value);
		has_pressure = true;
	}
	if ((primary_sampled && (sensor_channel_get(primary, SENSOR_CHAN_HUMIDITY, &value) == 0)) ||
	    ((secondary_sampled &&
	      (sensor_channel_get(secondary, SENSOR_CHAN_HUMIDITY, &value) == 0)))) {
		tdf_tph.humidity = sensor_value_to_centi(&value);
		has_humidity = true;
	}

	/* Release power requirements */
	env_release(primary, primary_sampled);
	env_release(secondary, secondary_sampled);

	/* Log output TDFs */
	TASK_SCHEDULE_TDF_LOG(sch, TASK_ENVIRONMENTAL_LOG_TPH, TDF_AMBIENT_TEMP_PRES_HUM,
			      epoch_time_now(), &tdf_tph);
	TASK_SCHEDULE_TDF_LOG(sch, TASK_ENVIRONMENTAL_LOG_T, TDF_AMBIENT_TEMPERATURE,
			      epoch_time_now(), &tdf_temp);

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &tdf_tph, K_FOREVER);

	/* Print the measured values */
	LOG_INF("T=%6d mDeg P=%6d Pa H=%3d %%", tdf_tph.temperature,
		has_pressure ? tdf_tph.pressure : -1, has_humidity ? tdf_tph.humidity / 100 : -1);
}
