/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/auto/wifi_conn_log.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/drivers/watchdog.h>

#include <infuse/task_runner/tasks/tdf_logger.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

GATEWAY_HANDLER_DEFINE(udp_backhaul_handler, DEVICE_DT_GET(DT_NODELABEL(epacket_udp)));

int main(void)
{
	const struct device *tdf_logger_udp = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_bt_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	const struct device *epacket_udp = DEVICE_DT_GET(DT_NODELABEL(epacket_udp));

	/* Constant ePacket flags */
	epacket_global_flags_set(EPACKET_FLAGS_CLOUD_FORWARDING | EPACKET_FLAGS_CLOUD_SELF);
	epacket_udp_flags_set(EPACKET_FLAGS_UDP_ALWAYS_RX);

	/* Log Bluetooth connection events */
	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_BT_CONN_LOG_EVENTS_FLUSH);

	/* Log WiFi connection events */
	auto_wifi_conn_log_configure(TDF_DATA_LOGGER_SERIAL,
				     AUTO_WIFI_LOG_ALL | AUTO_WIFI_LOG_EVENTS_FLUSH);

	/* Start watchdog */
	infuse_watchdog_start();

	/* Gateway receive handlers */
	epacket_set_receive_handler(epacket_serial, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_adv, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_bt_central, udp_backhaul_handler);
	epacket_set_receive_handler(epacket_udp, udp_backhaul_handler);

	/* Always listening on Bluetooth advertising, serial and UDP */
	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);
	epacket_receive(epacket_udp, K_FOREVER);

	/* Send key identifiers on boot */
	epacket_send_key_ids(epacket_serial, K_FOREVER);

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);

	for (;;) {
		task_tdf_logger_manual_run(TDF_DATA_LOGGER_UDP, 0, TASK_TDF_LOGGER_LOG_ANNOUNCE,
					   NULL);
		tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);

		LOG_INF("Sent uptime %d on %s", k_uptime_seconds(), tdf_logger_udp->name);
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
