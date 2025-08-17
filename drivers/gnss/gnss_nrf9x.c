/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * This "driver" exists purely to provide a devicetree handle to higher-level
 * code that expects one. Actual usage of the built-in GNSS modem should be
 * done directly through the API in `nrf_modem_gnss.h`.
 */

#include <zephyr/device.h>

#define DT_DRV_COMPAT nordic_nrf9x_gnss

static int nrf9x_gnss_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define NRF9X_GNSS_INST(inst)                                                                      \
	DEVICE_DT_INST_DEFINE(inst, nrf9x_gnss_init, NULL, NULL, NULL, POST_KERNEL,                \
			      CONFIG_SENSOR_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(NRF9X_GNSS_INST);
