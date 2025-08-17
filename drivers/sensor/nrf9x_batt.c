/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>

#include <infuse/lib/nrf_modem_monitor.h>

#include <nrf_modem_at.h>

#define DT_DRV_COMPAT nordic_nrf9x_batt

struct nrf9x_batt_data {
	int voltage_mv;
};

static int nrf9x_batt_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct nrf9x_batt_data *data = dev->data;
	int rc;

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_VOLTAGE);

#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR
	if (!nrf_modem_monitor_is_at_safe()) {
		return -EAGAIN;
	}
#endif
	rc = nrf_modem_at_scanf("AT%XVBAT", "%%XVBAT: %d", &data->voltage_mv);
	return rc == 1 ? 0 : -EIO;
}

static int nrf9x_batt_channel_get(const struct device *dev, enum sensor_channel chan,
				  struct sensor_value *val)
{
	struct nrf9x_batt_data *data = dev->data;

	if (chan != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}

	return sensor_value_from_milli(val, data->voltage_mv);
}

static const struct sensor_driver_api nrf9x_batt_driver_api = {
	.sample_fetch = nrf9x_batt_sample_fetch,
	.channel_get = nrf9x_batt_channel_get,
};

int nrf9x_batt_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define NRF9X_BATT_DEFINE(inst)                                                                    \
	static struct nrf9x_batt_data nrf9x_batt_data_##inst;                                      \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, nrf9x_batt_init, NULL, &nrf9x_batt_data_##inst, NULL,   \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &nrf9x_batt_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NRF9X_BATT_DEFINE)
