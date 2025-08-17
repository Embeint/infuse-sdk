/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

int main(void)
{
	const struct device *lpuart = DEVICE_DT_GET_ONE(nordic_nrf_sw_lpuart);

	printk("LPUART: %p %s\n", lpuart, lpuart->name);

	return 0;
}
