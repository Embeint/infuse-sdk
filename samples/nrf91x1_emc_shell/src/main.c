/**
 * @file
 * @copyright 2026 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/pm/device_runtime.h>

#include <modem/nrf_modem_lib.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

void nrf_modem_fault_handler(struct nrf_modem_fault_info *fault)
{
	LOG_ERR("Modem fault %d @ 0x%08x", fault->reason, fault->program_counter);
}

int main(void)
{
	const struct device *shell_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	int rc;

	if (!device_is_ready(shell_uart)) {
		LOG_ERR("UART '%s' not ready", shell_uart->name);
		return -ENODEV;
	}

	if (pm_device_runtime_get(shell_uart) != 0) {
		LOG_ERR("Failed to start UART '%s'", shell_uart->name);
		return -ENODEV;
	}

	rc = nrf_modem_lib_init();
	if (rc != 0) {
		LOG_ERR("Failed to initialise nRF91 modem (%d)", rc);
		return rc;
	}

	k_sleep(K_FOREVER);
}
