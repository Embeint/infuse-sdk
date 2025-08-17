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

void environmental_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct device *env = task->executor.workqueue.task_arg.const_arg;
	struct tdf_ambient_temp_pres_hum tdf_tph;
	struct tdf_ambient_temperature tdf_temp;
	struct sensor_value value;
	bool has_pressure;
	bool has_humidity;
	int rc;

	/* Request sensor to be powered */
	rc = pm_device_runtime_get(env);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "PM failure");
		return;
	}

	/* Trigger the sample */
	rc = sensor_sample_fetch(env);
	if (rc < 0) {
		LOG_ERR("Terminating due to %s", "fetch failure");
		return;
	}

	/* Populate the output TDFs */
	rc = sensor_channel_get(env, SENSOR_CHAN_AMBIENT_TEMP, &value);
	tdf_tph.temperature = sensor_value_to_milli(&value);
	tdf_temp.temperature = tdf_tph.temperature;
	rc = sensor_channel_get(env, SENSOR_CHAN_PRESS, &value);
	has_pressure = rc == 0;
	tdf_tph.pressure = has_pressure ? sensor_value_to_milli(&value) : 0;
	rc = sensor_channel_get(env, SENSOR_CHAN_HUMIDITY, &value);
	has_humidity = rc == 0;
	tdf_tph.humidity = has_humidity ? sensor_value_to_centi(&value) : 0;

	/* Release power requirement */
	rc = pm_device_runtime_put(env);
	if (rc < 0) {
		LOG_ERR("PM put failure");
	}

	/* Log output TDFs */
	TASK_SCHEDULE_TDF_LOG(sch, TASK_ENVIRONMENTAL_LOG_TPH, TDF_AMBIENT_TEMP_PRES_HUM,
			      epoch_time_now(), &tdf_tph);
	TASK_SCHEDULE_TDF_LOG(sch, TASK_ENVIRONMENTAL_LOG_T, TDF_AMBIENT_TEMPERATURE,
			      epoch_time_now(), &tdf_temp);

	/* Publish new data reading */
	zbus_chan_pub(ZBUS_CHAN, &tdf_tph, K_FOREVER);

	/* Print the measured values */
	LOG_INF("%s: T=%6d mDeg P=%6d Pa H=%3d %%", env->name, tdf_tph.temperature,
		has_pressure ? tdf_tph.pressure : -1, has_humidity ? tdf_tph.humidity / 100 : -1);
}
