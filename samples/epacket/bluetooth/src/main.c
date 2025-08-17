/**
 * @file
 * @copyright 2024 Embeint Inc
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

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/version.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void bt_periph_interface_state(uint16_t max_payload, void *user);

struct epacket_interface_cb bt_periph_interface_cb = {
	.interface_state = bt_periph_interface_state,
};

static void bt_periph_interface_state(uint16_t max_payload, void *user)
{
	ARG_UNUSED(user);

	LOG_INF("BT PERIPH: %d", max_payload);
}

int main(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	struct infuse_version version = application_version_get();
	struct tdf_announce announce = {0};

	(void)KV_STORE_READ(KV_KEY_REBOOTS, &reboots);

	epacket_register_callback(epacket_bt_periph, &bt_periph_interface_cb);

	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);

	announce.application = CONFIG_INFUSE_APPLICATION_ID;
	announce.reboots = reboots.count;
	announce.version.major = version.major;
	announce.version.minor = version.minor;
	announce.version.revision = version.revision;
	announce.version.build_num = version.build_num;

	for (;;) {
		announce.uptime = k_uptime_get() / 1000;

		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
		LOG_INF("Sent announce %d on Advertising and GATT", announce.uptime);

		k_sleep(K_SECONDS(1));
	}
	return 0;
}
