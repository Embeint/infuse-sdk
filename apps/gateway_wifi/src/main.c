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
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	KV_STRING_CONST(wifi_ssid, CONFIG_WIFI_SSID);
	KV_STRING_CONST(wifi_psk, CONFIG_WIFI_PSK);
	const struct device *epacket_udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	struct net_buf *buf;
	int cnt = 0;
	uint8_t auth;

	/* Write WiFi configuration */
	kv_store_write(KV_KEY_WIFI_SSID, &wifi_ssid, sizeof(wifi_ssid));
	kv_store_write(KV_KEY_WIFI_PSK, &wifi_psk, sizeof(wifi_psk));

	for (;;) {
		buf = epacket_alloc_tx_for_interface(epacket_udp, K_FOREVER);
		net_buf_add_le32(buf, cnt);

		auth = cnt & 0b1 ? EPACKET_AUTH_NETWORK : EPACKET_AUTH_DEVICE;

		epacket_set_tx_metadata(buf, auth, 0x12, 0xF0);
		epacket_queue(epacket_udp, buf);
		LOG_INF("Sent %s %d", epacket_udp->name, cnt++);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
