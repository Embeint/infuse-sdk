/**
 * Copyright (c) 2024 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/epacket/interface.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/definitions.h>

extern enum bst_result_t bst_result;
static int connection_notifications;
static int disconnection_notifications;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void peripheral_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	if (current_max_payload > 0) {
		connection_notifications += 1;
	} else {
		disconnection_notifications += 1;
	}
	LOG_INF("Peripheral: %s (Payload %d)",
		current_max_payload > 0 ? "Connected" : "Disconnected", current_max_payload);
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
	TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV, TDF_ANNOUNCE, 0, &announce);
	tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);

	LOG_INF("Starting legacy advertiser");

	if (bluetooth_legacy_advertising_run() < 0) {
		FAIL("Failed to start legacy advertiser\n");
		return;
	}

	/* Only push ePackets over GATT after that */
	for (int i = 0; i < 9; i++) {
		announce.uptime = k_uptime_seconds();
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_PERIPHERAL, TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);
		k_sleep(K_SECONDS(1));
	}

	PASS("Legacy advertising device complete\n");
}

#define NAME_1 "BOB"
#define NAME_2 "SALLY"

static void main_legacy_adv_name_update(void)
{
	KV_STRING_CONST(default_name, NAME_1);
	KV_STRING_CONST(updated_name, NAME_2);

	if (kv_store_write(KV_KEY_DEVICE_NAME, &default_name, sizeof(default_name)) < 0) {
		FAIL("Failed to write device name\n");
		return;
	}

	if (bluetooth_legacy_advertising_run() < 0) {
		FAIL("Failed to start legacy advertiser\n");
		return;
	}

	k_sleep(K_SECONDS(2));

	/* Write a new name to the store */
	if (kv_store_write(KV_KEY_DEVICE_NAME, &updated_name, sizeof(updated_name)) < 0) {
		FAIL("Failed to write updated device name\n");
		return;
	}

	k_sleep(K_SECONDS(2));

	/* Delete the name */
	if (kv_store_delete(KV_KEY_DEVICE_NAME) < 0) {
		FAIL("Failed to write delete device name\n");
		return;
	}

	k_sleep(K_SECONDS(2));

	PASS("Legacy advertising device complete\n");
}

static bool seen_name_1;
static bool seen_name_2;
static bool seen_name_default;

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	const char *name;

	/* Only look at legacy advertising packets with a name */
	if (adv_type != BT_GAP_ADV_TYPE_ADV_IND) {
		return;
	}
	if (buf->data[1] != BT_DATA_NAME_COMPLETE) {
		return;
	}
	name = buf->data + 2;

	/* Compare against the names we expect to see */
	if (buf->data[0] == (1 + strlen(NAME_1)) && strncmp(NAME_1, name, strlen(NAME_1)) == 0) {
		seen_name_1 = true;
	}
	if (buf->data[0] == (1 + strlen(NAME_2)) && strncmp(NAME_2, name, strlen(NAME_2)) == 0) {
		seen_name_2 = true;
	}
	if (buf->data[0] == (1 + strlen(CONFIG_BT_DEVICE_NAME)) &&
	    strncmp(CONFIG_BT_DEVICE_NAME, name, strlen(CONFIG_BT_DEVICE_NAME)) == 0) {
		seen_name_default = true;
	}
}

static void main_legacy_adv_name_watcher(void)
{
	static const struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		/* 32 * 0.625 = 20ms */
		.interval = 0x0020,
		.window = 0x0020,
	};
	int rc;

	/* Run scanning for 8 seconds */
	rc = bt_le_scan_start(&scan_param, scan_cb);
	if (rc < 0) {
		FAIL("Failed to start scanning\n");
	}

	k_sleep(K_SECONDS(8));

	(void)bt_le_scan_stop();

	/* Expect to have seen all 3 names */
	if (!seen_name_1) {
		FAIL("Failed to observe '%s'\n", NAME_1);
		return;
	}
	if (!seen_name_2) {
		FAIL("Failed to observe '%s'\n", NAME_2);
		return;
	}
	if (!seen_name_default) {
		FAIL("Failed to observe '%s'\n", CONFIG_BT_DEVICE_NAME);
		return;
	}

	PASS("Legacy advertising name watcher complete\n");
}

static void main_legacy_adv_expect_reboot(void)
{
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	struct tdf_announce announce = {0};
	struct epacket_interface_cb interface_cb = {
		.interface_state = peripheral_interface_state,
	};
	struct k_sem *reboot_sem = test_get_reboot_sem();
	int rc;

	epacket_register_callback(epacket_bt_periph, &interface_cb);

	LOG_INF("Single ePacket to simplify peer discovery");
	k_sleep(K_MSEC(100));
	TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV, TDF_ANNOUNCE, 0, &announce);
	tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);

	LOG_INF("Starting legacy advertiser");

	if (bluetooth_legacy_advertising_run() < 0) {
		FAIL("Failed to start legacy advertiser\n");
		return;
	}

	/* Wait for infuse_reboot or infuse_reboot_delayable to be called */
	rc = k_sem_take(reboot_sem, K_SECONDS(5));
	if (rc != 0) {
		FAIL("Failed to be rebooted\n");
		return;
	}
	PASS("Device rebooted\n");

	/* Give the connection time to terminate */
	k_sleep(K_SECONDS(2));
}

static const struct bst_test_instance legacy_adv_advertiser[] = {
	{
		.test_id = "epacket_bt_legacy_adv",
		.test_descr = "Basic Infuse-IoT Bluetooth device advertising on legacy channels",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_epacket_bt_legacy_broadcast,
	},
	{
		.test_id = "legacy_adv_name_update",
		.test_descr = "Test updating the device name through KV store",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_legacy_adv_name_update,
	},
	{
		.test_id = "legacy_adv_name_scanner",
		.test_descr = "Watch names being advertised",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_legacy_adv_name_watcher,
	},
	{
		.test_id = "legacy_adv_expect_reboot",
		.test_descr = "Expect to be rebooted",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_legacy_adv_expect_reboot,
	},
	BSTEST_END_MARKER,
};

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
