/**
 * @file
 * @copyright 2024 Embeint Inc
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
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	KV_STRING_CONST(wifi_ssid, CONFIG_WIFI_SSID);
	KV_STRING_CONST(wifi_psk, CONFIG_WIFI_PSK);
	const struct device *tdf_logger_udp = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
	struct tdf_announce announce;

	/* Write WiFi configuration */
	kv_store_write(KV_KEY_WIFI_SSID, &wifi_ssid, sizeof(wifi_ssid));
	kv_store_write(KV_KEY_WIFI_PSK, &wifi_psk, sizeof(wifi_psk));

	for (;;) {
		announce.application = 0x1234;
		announce.reboots = 0;
		announce.uptime = k_uptime_get() / 1000;
		announce.version = (struct tdf_struct_mcuboot_img_sem_ver){
			.major = 1,
			.minor = 2,
			.revision = 3,
			.build_num = 4,
		};

		tdf_data_logger_log_dev(tdf_logger_udp, TDF_ANNOUNCE, (sizeof(announce)), 0, &announce);
		tdf_data_logger_flush_dev(tdf_logger_udp);

		LOG_INF("Sent uptime %d on %s", announce.uptime, tdf_logger_udp->name);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
