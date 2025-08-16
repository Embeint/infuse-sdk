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

#include <infuse/auto/bluetooth_conn_log.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/packet.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>

extern enum bst_result_t bst_result;
static K_SEM_DEFINE(epacket_adv_received, 0, 1);
static K_SEM_DEFINE(char_read_received, 0, 1);
static bt_addr_le_t adv_device;
static atomic_t received_packets;

static uint8_t char_read_data[128];
static uint8_t char_read_result;
static size_t char_read_len;

static const struct bt_uuid_128 command_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_COMMAND_VAL);
static const struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_DATA_VAL);
static const struct bt_uuid_128 logging_uuid = BT_UUID_INIT_128(INFUSE_SERVICE_UUID_LOGGING_VAL);
static const struct bt_uuid *infuse_iot_characteristics[] = {
	(const void *)&command_uuid,
	(const void *)&data_uuid,
	(const void *)&logging_uuid,
};

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void common_init(void)
{
	k_sem_reset(&epacket_adv_received);
	k_sem_reset(&char_read_received);
	received_packets = 0;
}

static int expect_no_serial_tdf(void)
{
	struct k_fifo *fifo = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	/* Flush logger and confirm no information logged */
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	buf = k_fifo_get(fifo, K_MSEC(10));
	if (buf) {
		net_buf_unref(buf);
		return -1;
	}
	return 0;
}

static struct net_buf *expect_serial_tdf(struct tdf_parsed *tdf, uint16_t tdf_id, bool auto_flush)
{
	struct k_fifo *fifo = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	if (!auto_flush) {
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	}
	buf = k_fifo_get(fifo, K_MSEC(10));
	if (buf == NULL) {
		return NULL;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate logged TDF */
	if (tdf_parse_find_in_buf(buf->data, buf->len, tdf_id, tdf) != 0) {
		net_buf_unref(buf);
		return NULL;
	}

	return buf;
}

static int expect_bt_conn_tdf(uint8_t state, bool auto_flush)
{
	struct tdf_bluetooth_connection *bt_conn;
	struct net_buf *buf;
	struct tdf_parsed tdf;

	k_sleep(K_MSEC(10));
	buf = expect_serial_tdf(&tdf, TDF_BLUETOOTH_CONNECTION, auto_flush);
	if (buf == NULL) {
		return -1;
	}
	bt_conn = tdf.data;
	if ((tdf.tdf_num != 1) || (bt_conn->connected != state)) {
		return -1;
	}
	if (auto_flush && (tdf.time != 0)) {
		return -1;
	}
	if (!auto_flush && (tdf.time == 0)) {
		return -1;
	}
	net_buf_unref(buf);
	return 0;
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
	struct bt_conn *conn = NULL;
	bt_addr_le_t addr;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}
	addr.a.val[0]++;

	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_SERIAL, 0);

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, NULL, &callbacks, BT_GAP_LE_PHY_NONE);

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
		conn = NULL;
		k_sleep(K_MSEC(200));

		expect_no_serial_tdf();
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
	struct bt_conn *conn = NULL;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_SERIAL, 0);

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, NULL, &callbacks, BT_GAP_LE_PHY_NONE);

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
		if (expect_bt_conn_tdf(1, false) != 0) {
			FAIL("Failed to get expected TDF\n");
			return;
		}

		k_sleep(K_MSEC(100));
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;

		if (expect_bt_conn_tdf(0, false) != 0) {
			FAIL("Failed to get expected TDF\n");
			return;
		}
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
	struct bt_conn *conn = NULL;
	bt_addr_le_t addr;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	auto_bluetooth_conn_log_configure(TDF_DATA_LOGGER_SERIAL, AUTO_BT_CONN_LOG_EVENTS_FLUSH);

	for (int i = 0; i < 3; i++) {
		k_poll_signal_reset(&sig);
		events[0].state = K_POLL_STATE_NOT_READY;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, &discovery, &callbacks, BT_GAP_LE_PHY_NONE);

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

		if (expect_bt_conn_tdf(1, true) != 0) {
			FAIL("Failed to get expected TDF\n");
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
		conn = NULL;

		if (expect_bt_conn_tdf(0, true) != 0) {
			FAIL("Failed to get expected TDF\n");
			return;
		}
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
	struct bt_conn *conn = NULL;
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
		bt_conn_le_auto_setup(conn, &discovery, &callbacks, BT_GAP_LE_PHY_NONE);

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
		conn = NULL;
	}

	PASS("Connect discover nonexistant passed\n\n");
}

static void main_connect_discover_does_doesnt(void)
{
	const struct bt_uuid_16 timezone_uuid = BT_UUID_INIT_16(BT_UUID_GATT_TZ_VAL);
	struct k_poll_signal sig;
	struct bt_gatt_remote_char remote_info[2] = {0};
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
	/* Exists then doesn't exist */
	const struct bt_uuid *characteristics[] = {
		(const void *)&data_uuid,
		(const void *)&timezone_uuid,
	};
	struct bt_conn_auto_discovery discovery = {
		.characteristics = characteristics,
		.cache = NULL,
		.remote_info = remote_info,
		.num_characteristics = 2,
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn = NULL;
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
		bt_conn_le_auto_setup(conn, &discovery, &callbacks, BT_GAP_LE_PHY_NONE);

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

		/* First characteristic should have been found, second not */
		if ((remote_info[0].value_handle == 0x0000) ||
		    (remote_info[0].ccc_handle == 0x0000)) {
			FAIL("Expected characteristic not discovered\n");
			return;
		}
		if ((remote_info[1].value_handle != 0x0000) ||
		    (remote_info[1].ccc_handle != 0x0000)) {
			FAIL("Unexpected characteristic discovered\n");
			return;
		}

		/* Disconnect from peer */
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	PASS("Connect discover mix 1 passed\n\n");
}

static void main_connect_discover_doesnt_does(void)
{
	const struct bt_uuid_16 timezone_uuid = BT_UUID_INIT_16(BT_UUID_GATT_TZ_VAL);
	struct k_poll_signal sig;
	struct bt_gatt_remote_char remote_info[2] = {0};
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
	/* Doesn't exist then exists */
	const struct bt_uuid *characteristics[] = {
		(const void *)&timezone_uuid,
		(const void *)&data_uuid,
	};
	struct bt_conn_auto_discovery discovery = {
		.characteristics = characteristics,
		.cache = NULL,
		.remote_info = remote_info,
		.num_characteristics = 2,
	};
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig),
	};
	unsigned int signaled;
	struct bt_conn *conn = NULL;
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
		bt_conn_le_auto_setup(conn, &discovery, &callbacks, BT_GAP_LE_PHY_NONE);

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

		/* First characteristic should not be found, second noshould */
		if ((remote_info[0].value_handle != 0x0000) ||
		    (remote_info[0].ccc_handle != 0x0000)) {
			FAIL("Unexpected characteristic discovered\n");
			return;
		}
		if ((remote_info[1].value_handle == 0x0000) ||
		    (remote_info[1].ccc_handle == 0x0000)) {
			FAIL("Expected characteristic not discovered\n");
			return;
		}

		/* Disconnect from peer */
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	PASS("Connect discover mix 2 passed\n\n");
}

static void main_connect_rssi(void)
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
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	bt_addr_le_t addr;
	struct tdf_bluetooth_rssi *bt_rssi;
	struct tdf_parsed tdf;
	int conn_rc;
	int rc;

	common_init();
	k_poll_signal_init(&sig);
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Initiate connection */
	rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
	if (rc < 0) {
		FAIL("Failed to initiate connection\n");
		return;
	}
	bt_conn_le_auto_setup(conn, NULL, &callbacks, BT_GAP_LE_PHY_NONE);

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

	/* -59 dBm is the default PHY RSSI */
	if (bt_conn_rssi(conn) != -59) {
		FAIL("Unexpected RSSI\n");
		return;
	}

	/* No logging by default */
	k_sleep(K_SECONDS(2));
	if (expect_no_serial_tdf() != 0) {
		FAIL("Unexpected packet\n");
		return;
	}

	/* Request RSSI logging */
	bt_conn_rssi_log(conn, TDF_DATA_LOGGER_SERIAL);
	for (int i = 0; i < 3; i++) {
		/* Wait for next log interval */
		k_sleep(K_MSEC(CONFIG_BT_CONN_AUTO_RSSI_INTERVAL_MS + 10));

		buf = expect_serial_tdf(&tdf, TDF_BLUETOOTH_RSSI, false);
		if (buf == NULL) {
			FAIL("Unexpected TDF data\n");
			return;
		}
		bt_rssi = tdf.data;
		if ((tdf.time == 0) || (tdf.tdf_num != 1) || (bt_rssi->address.type != addr.type) ||
		    (memcmp(bt_rssi->address.val, addr.a.val, 6) != 0) || (bt_rssi->rssi != -59)) {
			FAIL("Unexpected TDF data\n");
			return;
		}
		net_buf_unref(buf);
	}

	/* Disconnect from peer */
	rc = bt_conn_disconnect_sync(conn);
	if (rc < 0) {
		FAIL("Failed to disconnect from peer\n");
		return;
	}
	bt_conn_unref(conn);

	PASS("Connect RSSI passed\n\n");
}

static int connect_with_phy(bt_addr_le_t *addr, uint8_t requested_phy, uint8_t expected_phy)
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
	struct bt_conn *conn = NULL;
	struct bt_conn_info info;
	int conn_rc;
	int rc;

	k_poll_signal_init(&sig);

	/* Initiate connection */
	conn = NULL;
	k_poll_signal_reset(&sig);
	rc = bt_conn_le_create(addr, &create_param, &conn_params, &conn);
	if (rc < 0) {
		FAIL("Failed to initiate connection\n");
		return -1;
	}

	/* Request the connection to use the 1 Mbit PHY */
	bt_conn_le_auto_setup(conn, NULL, &callbacks, requested_phy);

	/* Wait for connection process to complete */
	rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(3));
	k_poll_signal_check(&sig, &signaled, &conn_rc);
	if ((rc != 0) || (signaled == 0)) {
		FAIL("Signal not raised on connection\n");
		return -1;
	}
	if (conn_rc != 0) {
		FAIL("Unexpected connection result\n");
		return -1;
	}

	rc = bt_conn_get_info(conn, &info);
	if (rc < 0) {
		FAIL("Failed to query connection info\n");
		return -1;
	}

	if ((info.le.phy->rx_phy != expected_phy) || (info.le.phy->tx_phy != expected_phy)) {
		FAIL("Expected PHY not set (%d %d)\n", info.le.phy->rx_phy, info.le.phy->tx_phy);
		return -1;
	}

	/* Disconnect from peer */
	rc = bt_conn_disconnect_sync(conn);
	if (rc < 0) {
		FAIL("Failed to disconnect from peer\n");
		return -1;
	}
	bt_conn_unref(conn);
	return 0;
}

static void main_connect_phy(void)
{
	bt_addr_le_t addr;

	common_init();
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* PHY should update from 2M to 1M */
	if (connect_with_phy(&addr, BT_GAP_LE_PHY_1M, BT_GAP_LE_PHY_1M) < 0) {
		return;
	}
	/* PHY should remain 2M */
	if (connect_with_phy(&addr, BT_GAP_LE_PHY_2M, BT_GAP_LE_PHY_2M) < 0) {
		return;
	}
	/* PHY should remain 2M when unsupported is requested */
	if (connect_with_phy(&addr, BT_GAP_LE_PHY_CODED, BT_GAP_LE_PHY_2M) < 0) {
		return;
	}

	PASS("Connect preferred PHY passed\n\n");
}

static void run_connect_terminator(uint8_t phy)
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
	struct bt_conn *conn = NULL;
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
		conn = NULL;

		/* Initiate connection */
		rc = bt_conn_le_create(&addr, &create_param, &conn_params, &conn);
		if (rc < 0) {
			FAIL("Failed to initiate connection\n");
			return;
		}
		bt_conn_le_auto_setup(conn, &discovery, &callbacks, phy);

		/* Wait for connection process to complete */
		rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(5));
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
		conn = NULL;
		k_sleep(K_MSEC(250));
	} while (conn_rc != 0);

	PASS("Connect terminator passed\n\n");
}

static void main_connect_terminator(void)
{
	run_connect_terminator(BT_GAP_LE_PHY_NONE);
}

static void main_connect_terminator_phy(void)
{
	run_connect_terminator(BT_GAP_LE_PHY_1M);
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
		.test_id = "gatt_connect_discover_does_doesnt",
		.test_descr = "Connect and discover a characteristics that does and doesn't exist",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_discover_does_doesnt,
	},
	{
		.test_id = "gatt_connect_discover_doesnt_does",
		.test_descr = "Connect and discover a characteristics that doesn't and does exist",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_discover_doesnt_does,
	},
	{
		.test_id = "gatt_connect_rssi",
		.test_descr = "Monitor connection RSSI",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_rssi,
	},
	{
		.test_id = "gatt_connect_phy",
		.test_descr = "Connect with a preferred PHY",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_phy,
	},
	{
		.test_id = "gatt_connect_terminator",
		.test_descr = "Connect to device that keeps disconnecting",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_terminator,
	},
	{
		.test_id = "gatt_connect_terminator_phy",
		.test_descr = "Connect to device that keeps disconnecting with a preferred PHY",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_connect_terminator_phy,
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
