/**
 * Copyright (c) 2024 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/random/random.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/work_q.h>

extern enum bst_result_t bst_result;
static int connection_notifications;
static int disconnection_notifications;

static K_SEM_DEFINE(load_complete, 0, 1);

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#ifdef CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS

int infuse_board_public_bt_addr(bt_addr_le_t *addr)
{
	uint32_t nordic_oui = 0xF4CE36;

	/* Generate a random Nordic MAC address for testing */
	addr->type = BT_ADDR_LE_PUBLIC;
	sys_put_le24(nordic_oui, addr->a.val + 3);
	sys_put_le24(sys_rand32_get(), addr->a.val + 0);
	return 0;
}

#endif /* CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS */

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

static void main_epacket_bt_basic_broadcast(void)
{
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	struct tdf_announce announce = {0};
	struct epacket_interface_cb interface_cb = {
		.interface_state = peripheral_interface_state,
	};
	uint16_t packet_size;

#ifdef CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS
	bt_addr_le_t bt_addr[CONFIG_BT_ID_MAX];
	size_t bt_addr_cnt = ARRAY_SIZE(bt_addr);

	bt_id_get(bt_addr, &bt_addr_cnt);

	/* Ensure public address was set correctly */
	if (bt_addr->type != BT_ADDR_LE_PUBLIC) {
		FAIL("Public Bluetooth address not set\n");
		return;
	}
#endif /* CONFIG_INFUSE_BOARD_HAS_PUBLIC_BT_ADDRESS */

#ifdef CONFIG_DATA_LOGGER_SHIM
	const struct device *data_logger = DEVICE_DT_GET_ONE(embeint_data_logger_shim);
	const uint32_t num_blocks =
		DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(embeint_data_logger_shim), physical_blocks);
	uint8_t dummy_data[16] = {0};

	for (int i = 0; i < num_blocks; i++) {
		data_logger_block_write(data_logger, i, dummy_data, sizeof(dummy_data));
	}
#endif /* CONFIG_DATA_LOGGER_SHIM */

	epacket_register_callback(epacket_bt_periph, &interface_cb);

	packet_size = epacket_interface_max_packet_size(epacket_bt_periph);
	if (packet_size != 0) {
		FAIL("Unexpected packet size\n");
		return;
	}

	LOG_INF("Starting send");

	/* Burst send some packets */
	for (int i = 0; i < 5; i++) {
		k_sleep(K_USEC(sys_rand32_get() % 10000));
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
	}
	k_sleep(K_MSEC(500));

	/* Send 5 packets with spacing */
	for (int i = 0; i < 8; i++) {
		k_sleep(K_MSEC(1000));
		k_sleep(K_USEC(sys_rand32_get() % 10000));
		LOG_INF("TX %d", i);
		announce.uptime = k_uptime_seconds();
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
	}
	k_sleep(K_MSEC(1000));

	if (connection_notifications != disconnection_notifications) {
		FAIL("Unbalanced notifications\n");
		return;
	}

	PASS("Advertising device complete\n");
}

static void epacket_adv_load(struct k_work *work)
{
	const int iterations = 10 * CONFIG_EPACKET_BUFFERS_TX;
	struct tdf_announce announce = {0};

	LOG_INF("Loaded send from %p", work);

	for (int i = 0; i < iterations; i++) {
		LOG_INF("Loaded send %2d/%2d", i + 1, iterations);
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV, TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV);
	}
	k_sem_give(&load_complete);
}

static void main_epacket_bt_adv_loaded(void)
{
	struct k_work from_infuse;

	/* Heavy load from main application thread */
	epacket_adv_load(NULL);
	k_sem_take(&load_complete, K_FOREVER);

	/* Heavy load from the Infuse-IoT workqueue */
	k_work_init(&from_infuse, epacket_adv_load);
	infuse_work_submit(&from_infuse);
	k_sem_take(&load_complete, K_FOREVER);

	PASS("Loaded send complete\n");
}

static K_SEM_DEFINE(tx_done, 0, 1);
static const struct device *tx_cb_dev;
static struct net_buf *tx_cb_pkt;
static int tx_cb_result;

static void tx_done_cb(const struct device *dev, struct net_buf *pkt, int result, void *user_data)
{
	tx_cb_dev = dev;
	tx_cb_pkt = pkt;
	tx_cb_result = result;
	k_sem_give(&tx_done);
}

static void main_epacket_bt_periph_send_unconnected(void)
{
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	union epacket_interface_address dest;
	struct net_buf *buf;

	dest.bluetooth.type = BT_ADDR_LE_PUBLIC;
	for (int i = 0; i < sizeof(dest.bluetooth.a.val); i++) {
		dest.bluetooth.a.val[i] = 0x10 + i;
	}

	/* To an explicit device should fail */
	for (int i = 0; i < 5 * CONFIG_EPACKET_BUFFERS_TX; i++) {
		/* Send a packet to a device we're not connected to */
		buf = epacket_alloc_tx_for_interface(epacket_bt_periph, K_FOREVER);
		if (buf == NULL) {
			FAIL("No buffer\n");
			return;
		}
		epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, dest);
		epacket_set_tx_callback(buf, tx_done_cb, NULL);
		epacket_queue(epacket_bt_periph, buf);

		if (k_sem_take(&tx_done, K_MSEC(100)) != 0) {
			FAIL("No send callback\n");
			return;
		}
		if (tx_cb_dev != epacket_bt_periph) {
			FAIL("Bad device pointer\n");
			return;
		}
		if (tx_cb_pkt == NULL) {
			FAIL("Bad buffer pointer\n");
			return;
		}
		if (tx_cb_result != -ENOTCONN) {
			FAIL("Unexpected result\n");
			return;
		}
	}

	/* When no connected device exists */
	for (int i = 0; i < 5 * CONFIG_EPACKET_BUFFERS_TX; i++) {
		/* Send a packet to a device we're not connected to */
		buf = epacket_alloc_tx_for_interface(epacket_bt_periph, K_FOREVER);
		if (buf == NULL) {
			FAIL("No buffer\n");
			return;
		}
		epacket_set_tx_metadata(buf, EPACKET_AUTH_DEVICE, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
		epacket_set_tx_callback(buf, tx_done_cb, NULL);
		epacket_queue(epacket_bt_periph, buf);

		if (k_sem_take(&tx_done, K_MSEC(100)) != 0) {
			FAIL("No send callback\n");
			return;
		}
		if (tx_cb_dev != epacket_bt_periph) {
			FAIL("Bad device pointer\n");
			return;
		}
		if (tx_cb_pkt == NULL) {
			FAIL("Bad buffer pointer\n");
			return;
		}
		/* Sending to everyone when no-one is connected is not an error */
		if (tx_cb_result != 0) {
			FAIL("Unexpected result\n");
			return;
		}
	}

	PASS("Send to unconnected passed\n");
}

static const struct bst_test_instance ext_adv_advertiser[] = {
	{
		.test_id = "epacket_bt_device",
		.test_descr = "Basic Infuse-IoT Bluetooth device",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_epacket_bt_basic_broadcast,
	},
	{
		.test_id = "epacket_bt_adv_load",
		.test_descr = "Load the Bluetooth stack with large amounts of traffic",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_epacket_bt_adv_loaded,
	},
	{
		.test_id = "epacket_bt_periph_send_unconnected",
		.test_descr = "Queue packets to an unconnected device",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_epacket_bt_periph_send_unconnected,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_ext_adv_advertiser(struct bst_test_list *tests)
{
	return bst_add_tests(tests, ext_adv_advertiser);
}

bst_test_install_t test_installers[] = {test_ext_adv_advertiser, NULL};

int main(void)
{
	bst_main();
	return 0;
}
