/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/drivers/watchdog.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

GATEWAY_HANDLER_DEFINE(udp_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_udp)));

int main(void)
{
	const struct device *tdf_logger_udp = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	const struct device *epacket_udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));
	struct tdf_announce announce;

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers */
	epacket_set_receive_handler(epacket_serial, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_adv, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_udp, udp_backhaul_handler);

	/* Always listening on Bluetooth, serial and UDP */
	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);
	epacket_receive(epacket_udp, K_FOREVER);

	/* Turn on the interface */
	conn_mgr_all_if_up(false);
	conn_mgr_all_if_connect(false);

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

		tdf_data_logger_log_dev(tdf_logger_udp, TDF_ANNOUNCE, (sizeof(announce)), 0,
					&announce);
		tdf_data_logger_flush_dev(tdf_logger_udp);

		LOG_INF("Sent uptime %d on %s", announce.uptime, tdf_logger_udp->name);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
