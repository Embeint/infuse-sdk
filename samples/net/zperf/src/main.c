/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#ifdef CONFIG_SOC_NRF5340_CPUAPP
#include <nrfx_clock.h>
#endif /* CONFIG_SOC_NRF5340_CPUAPP */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));

#ifdef CONFIG_SOC_NRF5340_CPUAPP
	int err;

	/* For optimal performance, the CPU frequency needs to be set to 128 MHz */
	err = nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
	err -= NRFX_ERROR_BASE_NUM;
	if (err != 0) {
		LOG_WRN("Failed to set 128 MHz: %d", err);
	}
#endif /* CONFIG_SOC_NRF5340_CPUAPP */

	/* Always listening on Bluetooth and serial */
	epacket_receive(epacket_serial, K_FOREVER);

	/* Send key identifiers on boot */
	epacket_send_key_ids(epacket_serial, K_FOREVER);

	/* Always want network connectivity */
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

	/* Loop forever, zperf runs from the RPC context */
	for (;;) {
		LOG_INF("Uptime: %6d seconds", k_uptime_seconds());
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
