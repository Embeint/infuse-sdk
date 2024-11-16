/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/epacket/packet.h>
#include <infuse/bluetooth/gatt.h>

extern enum bst_result_t bst_result;
static K_SEM_DEFINE(epacket_adv_received, 0, 1);
static K_SEM_DEFINE(char_read_received, 0, 1);
static bt_addr_le_t adv_device;
static atomic_t received_packets;

static uint8_t char_read_data[128];
static uint8_t char_read_result;
static size_t char_read_len;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void common_init(void)
{
	k_sem_reset(&epacket_adv_received);
	k_sem_reset(&char_read_received);
	received_packets = 0;
}

static void epacket_bt_adv_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_INF("RX Type: %02X Flags: %04X Auth: %d Len: %d RSSI: %ddBm", meta->type, meta->flags,
		meta->auth, buf->len, meta->rssi);
	adv_device = meta->interface_address.bluetooth;
	atomic_inc(&received_packets);

	net_buf_unref(buf);

	k_sem_give(&epacket_adv_received);
}

static int observe_peer(bt_addr_le_t *addr)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));

	epacket_set_receive_handler(epacket_bt_adv, epacket_bt_adv_receive_handler);
	if (epacket_receive(epacket_bt_adv, K_FOREVER) < 0) {
		return -1;
	}

	/* Wait for packet so we know the peer address */
	if (k_sem_take(&epacket_adv_received, K_SECONDS(3)) < 0) {
		return -1;
	}
	*addr = adv_device;

	/* Zephyr Bluetooth controller doesn't support simultaneous scan + conn */
	if (epacket_receive(epacket_bt_adv, K_NO_WAIT) < 0) {
		return -1;
	}
	k_sleep(K_MSEC(10));
	return 0;
}

static uint8_t char_read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			    const void *data, uint16_t length)
{
	memcpy(char_read_data, data, length);
	char_read_len = length;
	k_sem_give(&char_read_received);
	return BT_GATT_ITER_STOP;
}

static void conn_setup_cb(struct bt_conn *conn, int err, void *user_data)
{
	struct k_poll_signal *sig = user_data;

	/* Notify command handler */
	k_poll_signal_raise(sig, -err);
}

static void main_connect_nonexistant(void)
{
	struct k_poll_signal sig;
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.conn_terminated_cb = NULL,
		.user_data = &sig,
	};
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		.timeout = 2000 / 10,
	};
	const struct bt_le_conn_param conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}
	addr.a.val[0]++;

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, NULL, &callbacks);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
		if (rc != 0) {
			FAIL("Signal not raised on timeout\n");
			return;
		}
		k_poll_signal_check(&sig, &signaled, &rc);
		if (signaled == 0) {
			FAIL("Signal not raised on timeout\n");
			return;
		}
		if (rc != -BT_HCI_ERR_UNKNOWN_CONN_ID) {
			FAIL("Unexpected error code on timeout\n");
			return;
		}
		bt_conn_unref(conn);
		k_sleep(K_MSEC(200));
	}

	PASS("Gateway connection timeout passed\n\n");
}

static void main_connect_no_discovery(void)
{
	struct k_poll_signal sig;
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.conn_terminated_cb = NULL,
		.user_data = &sig,
	};
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		.timeout = 2000 / 10,
	};
	const struct bt_le_conn_param conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, NULL, &callbacks);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
		k_poll_signal_check(&sig, &signaled, &conn_rc);
		if ((rc != 0) || (signaled == 0)) {
			FAIL("Signal not raised on connection\n");
			return;
		}
		if (conn_rc != 0) {
			FAIL("Unexpected connection result\n");
			return;
		}
		k_sleep(K_MSEC(100));
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
	}

	PASS("Connect without discovery passed\n\n");
}

static void main_connect_discover_name(void)
{
	const struct bt_uuid_16 device_name_uuid = BT_UUID_INIT_16(BT_UUID_GAP_DEVICE_NAME_VAL);
	struct k_poll_signal sig;
	struct bt_gatt_remote_char remote_info[1] = {0};
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.conn_terminated_cb = NULL,
		.user_data = &sig,
	};
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		.timeout = 2000 / 10,
	};
	const struct bt_le_conn_param conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	const struct bt_uuid *characteristics[] = {
		(const void *)&device_name_uuid,
	};
	struct bt_conn_auto_discovery discovery = {
		.characteristics = characteristics,
		.cache = NULL,
		.remote_info = remote_info,
		.num_characteristics = ARRAY_SIZE(characteristics),
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, &discovery, &callbacks);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
		k_poll_signal_check(&sig, &signaled, &conn_rc);
		if ((rc != 0) || (signaled == 0)) {
			FAIL("Signal not raised on connection\n");
			return;
		}
		if (conn_rc != 0) {
			FAIL("Unexpected connection result\n");
			return;
		}

		/* Sanity check discovered values */
		if ((remote_info[0].value_handle == 0x0000) ||
		    (remote_info[0].ccc_handle != 0x0000)) {
			FAIL("Unexpected characteristic discovery\n");
			return;
		}

		/* Do a read to check it actually worked */
		struct bt_gatt_read_params read_params = {
			.func = char_read_cb,
			.handle_count = 1,
			.single =
				{
					.handle = remote_info[0].value_handle,
					.offset = 0,
				},
		};

		rc = bt_gatt_read(conn, &read_params);
		if (rc < 0) {
			FAIL("Failed to read from characteristic\n");
			return;
		}
		if (k_sem_take(&char_read_received, K_SECONDS(1)) != 0) {
			FAIL("Characteristic read did not complete\n");
			return;
		}
		if (char_read_result != BT_HCI_ERR_SUCCESS) {
			FAIL("Characteristic read failed\n");
			return;
		}
		if ((char_read_len != 10) || (memcmp(char_read_data, "Infuse-IoT", 10) != 0)) {
			FAIL("Unexpected characteristic data\n");
			return;
		}

		/* Disconnect from peer */
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
	}

	PASS("Connect discover name passed\n\n");
}

static void main_connect_discover_nonexistant(void)
{
	const struct bt_uuid_16 timezone_uuid = BT_UUID_INIT_16(BT_UUID_GATT_TZ_VAL);
	struct k_poll_signal sig;
	struct bt_gatt_remote_char remote_info[1] = {0};
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.conn_terminated_cb = NULL,
		.user_data = &sig,
	};
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		.timeout = 2000 / 10,
	};
	const struct bt_le_conn_param conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	const struct bt_uuid *characteristics[] = {
		(const void *)&timezone_uuid,
	};
	struct bt_conn_auto_discovery discovery = {
		.characteristics = characteristics,
		.cache = NULL,
		.remote_info = remote_info,
		.num_characteristics = 1,
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, &discovery, &callbacks);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
		k_poll_signal_check(&sig, &signaled, &conn_rc);
		if ((rc != 0) || (signaled == 0)) {
			FAIL("Signal not raised on connection\n");
			return;
		}
		if (conn_rc != 0) {
			FAIL("Unexpected connection result\n");
			return;
		}

		/* Sanity check discovered values */
		if ((remote_info[0].value_handle != 0x0000) ||
		    (remote_info[0].ccc_handle != 0x0000)) {
			FAIL("Unexpected characteristic discovery\n");
			return;
		}

		/* Disconnect from peer */
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
	}

	PASS("Connect discover nonexistant passed\n\n");
}

static const struct bt_uuid_128 command_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_COMMAND_VAL);
static const struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_DATA_VAL);
static const struct bt_uuid_128 logging_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_LOGGING_VAL);
static const struct bt_uuid *infuse_iot_characteristics[] = {
	(const void *)&command_uuid,
	(const void *)&data_uuid,
	(const void *)&logging_uuid,
};

static void main_connect_terminator(void)
{
	struct k_poll_signal sig;
	struct bt_gatt_remote_char remote_info[3] = {0};
	struct bt_conn_auto_setup_cb callbacks = {
		.conn_setup_cb = conn_setup_cb,
		.conn_terminated_cb = NULL,
		.user_data = &sig,
	};
	const struct bt_conn_le_create_param create_param = {
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_INTERVAL,
		.timeout = 2000 / 10,
	};
	const struct bt_le_conn_param conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct bt_conn_auto_discovery discovery = {
		.characteristics = infuse_iot_characteristics,
		.cache = NULL,
		.remote_info = remote_info,
		.num_characteristics = 3,
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Loop until the connection succeeds */
	do {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, &discovery, &callbacks);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
		k_poll_signal_check(&sig, &signaled, &conn_rc);
		if (signaled != 1) {
			FAIL("Result not signaled\n");
			return;
		}
		if (conn_rc == 0) {
			if ((remote_info[0].value_handle == 0) ||
			    (remote_info[1].value_handle == 0)) {
				FAIL("Characteristic discovery failed\n");
				return;
			}
			bt_conn_disconnect_sync(conn);
		}
		bt_conn_unref(conn);
		k_sleep(K_MSEC(250));
	} while (conn_rc != 0);

	PASS("Connect terminator passed\n\n");
}

static const struct bst_test_instance gatt_gateway[] = {
	{
		.test_id = "gatt_connect_nonexistant",
		.test_descr = "Try connecting to device that doesn't exist",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_nonexistant,
	},
	{
		.test_id = "gatt_connect_no_discovery",
		.test_descr = "Connect without characteristic discovery",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_no_discovery,
	},
	{
		.test_id = "gatt_connect_discover_name",
		.test_descr = "Connect and discover device name",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_discover_name,
	},
	{
		.test_id = "gatt_connect_discover_name",
		.test_descr = "Connect and discover device name",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_discover_name,
	},
	{
		.test_id = "gatt_connect_discover_nonexistant",
		.test_descr = "Connect and discover characteristic that doesn't exist",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_discover_nonexistant,
	},
	{
		.test_id = "gatt_connect_terminator",
		.test_descr = "Connect to device that keeps disconnecting",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_terminator,
	},
	BSTEST_END_MARKER};

struct bst_test_list *test_gatt_bt_gateway(struct bst_test_list *tests)
{
	return bst_add_tests(tests, gatt_gateway);
}

bst_test_install_t test_installers[] = {test_gatt_bt_gateway, NULL};

int main(void)
{
	bst_main();
	return 0;
}
