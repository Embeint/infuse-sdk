/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT zephyr_generic_sim_sensor

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <infuse/drivers/sensor/generic_sim.h>

struct generic_sim_data {
	struct sensor_value channel_values[SENSOR_CHAN_ALL];
	ATOMIC_DEFINE(channels_set, SENSOR_CHAN_ALL);
};

struct generic_sim_cfg {
	int init_rc;
};

LOG_MODULE_DECLARE(GENERIC_SIM, CONFIG_SENSOR_LOG_LEVEL);

void generic_sim_reset(const struct device *dev)
{
	struct generic_sim_data *data = dev->data;

	memset(data->channels_set, 0, sizeof(data->channels_set));
}

int generic_sim_channel_set(const struct device *dev, enum sensor_channel chan,
			    struct sensor_value val)
{
	struct generic_sim_data *data = dev->data;

	if (chan >= SENSOR_CHAN_ALL) {
		return -EINVAL;
	}

	atomic_set_bit(data->channels_set, chan);
	data->channel_values[chan] = val;
	return 0;
}

static int generic_sim_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	return 0;
}

static int generic_sim_channel_get(const struct device *dev, enum sensor_channel chan,
				   struct sensor_value *val)
{
	struct generic_sim_data *data = dev->data;

	if (chan >= SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}

	if (!atomic_test_bit(data->channels_set, chan)) {
		/* Channel that hasn't been configured */
		return -ENOTSUP;
	}

	*val = data->channel_values[chan];
	return 0;
}

static const struct sensor_driver_api generic_sim_driver_api = {
	.sample_fetch = generic_sim_sample_fetch,
	.channel_get = generic_sim_channel_get,
};

int generic_sim_init(const struct device *dev)
{
	const struct generic_sim_cfg *cfg = dev->config;

	return cfg->init_rc;
}

#define GENERIC_SIM_DEFINE(inst)                                                                   \
	static const struct generic_sim_cfg generic_sim_cfg_##inst = {                             \
		.init_rc = -DT_INST_PROP(inst, negated_init_rc),                                   \
	};                                                                                         \
	static struct generic_sim_data generic_sim_data_##inst;                                    \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, generic_sim_init, NULL, &generic_sim_data_##inst,       \
				     &generic_sim_cfg_##inst, POST_KERNEL,                         \
				     CONFIG_SENSOR_INIT_PRIORITY, &generic_sim_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GENERIC_SIM_DEFINE)
