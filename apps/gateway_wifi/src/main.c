/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Callback struct where the callback will be stored */
struct net_mgmt_event_callback l4_callback;

/* Callback handler */
static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event, struct net_if *iface)
{
	if (event == NET_EVENT_L4_CONNECTED) {
		LOG_WRN("Network connectivity gained!");
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		LOG_WRN("Network connectivity lost!");
	}
}

int main(void)
{
	KV_STRING_CONST(wifi_ssid, CONFIG_WIFI_SSID);
	KV_STRING_CONST(wifi_psk, CONFIG_WIFI_PSK);
	int cnt = 0;

	/* Write WiFi configuration */
	kv_store_write(KV_KEY_WIFI_SSID, &wifi_ssid, sizeof(wifi_ssid));
	kv_store_write(KV_KEY_WIFI_PSK, &wifi_psk, sizeof(wifi_psk));

	net_mgmt_init_event_callback(&l4_callback, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	/* Register the callback */
	net_mgmt_add_event_callback(&l4_callback);

	for (;;) {
		LOG_INF("Uptime: %d", cnt++);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
