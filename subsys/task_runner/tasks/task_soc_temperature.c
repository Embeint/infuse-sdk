/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/tasks/soc_temperature.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/zbus/channels.h>

LOG_MODULE_REGISTER(task_env, CONFIG_TASK_SOC_TEMPERATURE_LOG_LEVEL);

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE)

void soc_temperature_task_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);
	const struct device *dev = task->executor.workqueue.task_arg.const_arg;
	struct tdf_soc_temperature tdf;
	struct sensor_value val;
	int rc = 0;

	/* Fetch a sample */
	rc = sensor_sample_fetch(dev);
	if (rc != 0) {
		LOG_ERR("Failed to fetch from %s", dev->name);
		return;
	}
	rc = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &val);
	if (rc != 0) {
		LOG_ERR("Failed to retrieve reading from %s", dev->name);
		return;
	}

	LOG_INF("SoC Temperature: %d deg", val.val1);

	/* Convert to TDF units and publish on ZBUS */
	tdf.temperature = sensor_value_to_centi(&val);
	zbus_chan_pub(ZBUS_CHAN, &tdf, K_FOREVER);

	/* Log measurement */
	TASK_SCHEDULE_TDF_LOG(sch, TASK_SOC_TEMPERATURE_LOG_T, TDF_SOC_TEMPERATURE,
			      epoch_time_now(), &tdf);
}
