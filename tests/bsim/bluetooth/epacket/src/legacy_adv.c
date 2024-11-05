/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/epacket/interface.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

extern enum bst_result_t bst_result;
static int connection_notifications;
static int disconnection_notifications;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void peripheral_interface_state(bool connected, uint16_t current_max_payload, void *user_ctx)
{
	if (connected) {
		connection_notifications += 1;
	} else {
		disconnection_notifications += 1;
	}
	LOG_INF("Peripheral: %s (Payload %d)", connected ? "Connected" : "Disconnected",
		current_max_payload);
}

static void main_epacket_bt_legacy_broadcast(void)
{
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	struct tdf_announce announce = {0};
	struct epacket_interface_cb interface_cb = {
		.interface_state = peripheral_interface_state,
	};

	epacket_register_callback(epacket_bt_periph, &interface_cb);

	LOG_INF("Single ePacket to simplify peer discovery");
	k_sleep(K_MSEC(100));
	tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV, TDF_ANNOUNCE, (sizeof(announce)), 0, &announce);
	tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);

	LOG_INF("Starting legacy advertiser");

	if (bluetooth_legacy_advertising_run() < 0) {
		FAIL("Failed to start legacy advertiser\n");
		return;
	}

	/* Only push ePackets over GATT after that */
	for (int i = 0; i < 9; i++) {
		announce.uptime = k_uptime_seconds();
		tdf_data_logger_log(TDF_DATA_LOGGER_BT_PERIPHERAL, TDF_ANNOUNCE, (sizeof(announce)),
				    0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);
		k_sleep(K_SECONDS(1));
	}

	PASS("Legacy advertising device complete\n");
}

static const struct bst_test_instance legacy_adv_advertiser[] = {
	{.test_id = "epacket_bt_legacy_adv",
	 .test_descr = "Basic Infuse-IoT Bluetooth device advertising on legacy channels",
	 .test_pre_init_f = test_init,
	 .test_tick_f = test_tick,
	 .test_main_f = main_epacket_bt_legacy_broadcast},
	BSTEST_END_MARKER};

struct bst_test_list *test_legacy_adv(struct bst_test_list *tests)
{
	return bst_add_tests(tests, legacy_adv_advertiser);
}

bst_test_install_t test_installers[] = {test_legacy_adv, NULL};

int main(void)
{
	bst_main();
	return 0;
}
