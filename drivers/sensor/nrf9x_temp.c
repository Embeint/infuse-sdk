/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>

#include <infuse/lib/lte_modem_monitor.h>

#include <nrf_modem_at.h>

#define DT_DRV_COMPAT nordic_nrf9x_temp

struct nrf9x_temp_data {
	int temperature;
};

static int nrf9x_temp_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct nrf9x_temp_data *data = dev->data;
	int rc;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_DIE_TEMP);

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR
	if (!lte_modem_monitor_is_at_safe()) {
		return -EAGAIN;
	}
#endif
	rc = nrf_modem_at_scanf("AT%XTEMP?", "%%XTEMP: %d", &data->temperature);
	return rc == 1 ? 0 : -EIO;
}

static int nrf9x_temp_channel_get(const struct device *dev, enum sensor_channel chan,
				  struct sensor_value *val)
{
	struct nrf9x_temp_data *data = dev->data;

	if (chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	val->val1 = data->temperature;
	val->val2 = 0;
	return 0;
}

static const struct sensor_driver_api nrf9x_temp_driver_api = {
	.sample_fetch = nrf9x_temp_sample_fetch,
	.channel_get = nrf9x_temp_channel_get,
};

int nrf9x_temp_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define NRF9X_TEMP_DEFINE(inst)                                                                    \
	static struct nrf9x_temp_data nrf9x_temp_data_##inst;                                      \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, nrf9x_temp_init, NULL, &nrf9x_temp_data_##inst, NULL,   \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &nrf9x_temp_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NRF9X_TEMP_DEFINE)
