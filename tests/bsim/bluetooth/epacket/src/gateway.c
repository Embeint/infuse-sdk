/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/random/random.h>
#include <zephyr/bluetooth/hci_types.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/packet.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>
#include <infuse/security.h>
#include <infuse/states.h>

int epacket_bt_gatt_encrypt(struct net_buf *buf, uint32_t network_key_id);

uint8_t mem_buffer[1024];
extern enum bst_result_t bst_result;
static K_SEM_DEFINE(epacket_adv_received, 0, 1);
static K_SEM_DEFINE(bt_connected, 0, 1);
static K_SEM_DEFINE(bt_disconnected, 0, 1);
static bt_addr_le_t adv_device;
static atomic_t received_packets;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void connected(struct bt_conn *conn, uint8_t err)
{
	k_sem_give(&bt_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_sem_give(&bt_disconnected);
}

static struct bt_conn_cb conn_cb = {
	.connected = connected,
	.disconnected = disconnected,
};

static void common_init(void)
{
	k_sem_reset(&epacket_adv_received);
	k_sem_reset(&bt_connected);
	k_sem_reset(&bt_disconnected);
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

static void main_gateway_scan(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	int rc;

	common_init();
	epacket_set_receive_handler(epacket_bt_adv, epacket_bt_adv_receive_handler);
	rc = epacket_receive(epacket_bt_adv, K_FOREVER);
	if (rc < 0) {
		FAIL("Failed to start ePacket receive (%d)\n", rc);
		return;
	}

	LOG_INF("Waiting for packets");
	k_sleep(K_SECONDS(9));

	rc = epacket_receive(epacket_bt_adv, K_NO_WAIT);
	if (rc < 0) {
		FAIL("Failed to stop ePacket receive (%d)\n", rc);
		return;
	}

	if (atomic_get(&received_packets) < 10) {
		FAIL("Failed to receive expected packets\n");
	} else {
		PASS("Received %d packets from advertiser\n", atomic_get(&received_packets));
	}
}

static void main_gateway_scan_wdog(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	int rc;

	common_init();
	epacket_set_receive_handler(epacket_bt_adv, epacket_bt_adv_receive_handler);
	rc = epacket_receive(epacket_bt_adv, K_FOREVER);
	if (rc < 0) {
		FAIL("Failed to start ePacket receive (%d)\n", rc);
		return;
	}

	/* Pretend the controller is broken by manually stopping the scanning */
	rc = bt_le_scan_stop();
	if (rc < 0) {
		FAIL("Failed to manually stop Bluetooth scanning (%d)\n", rc);
	}

	LOG_INF("Expect the watchdog to restart the scanning after %d seconds",
		CONFIG_EPACKET_INTERFACE_BT_ADV_SCAN_WATCHDOG_SEC);
	k_sleep(K_SECONDS(9));

	rc = epacket_receive(epacket_bt_adv, K_NO_WAIT);
	if (rc < 0) {
		FAIL("Failed to stop ePacket receive (%d)\n", rc);
		return;
	}

	if (atomic_get(&received_packets) < 3) {
		FAIL("Failed to receive expected packets\n");
	} else {
		PASS("Received %d packets despite 'broken' controller\n",
		     atomic_get(&received_packets));
	}
}

static int observe_peers(bt_addr_le_t *addr, uint8_t num)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	uint8_t observed = 0;

	epacket_set_receive_handler(epacket_bt_adv, epacket_bt_adv_receive_handler);
	if (epacket_receive(epacket_bt_adv, K_FOREVER) < 0) {
		return -1;
	}

	while (observed < num) {
retry:
		/* Wait for packet so we know the peer address */
		if (k_sem_take(&epacket_adv_received, K_SECONDS(3)) < 0) {
			return -1;
		}
		/* Check if we already found this device */
		for (int i = 0; i < observed; i++) {
			if (bt_addr_le_cmp(&addr[i], &adv_device) == 0) {
				goto retry;
			}
		}
		addr[observed++] = adv_device;
	}

	/* Zephyr Bluetooth controller doesn't support simultaneous scan + conn */
	if (epacket_receive(epacket_bt_adv, K_NO_WAIT) < 0) {
		return -1;
	}
	k_sleep(K_MSEC(10));
	return 0;
}

static void main_gateway_connect(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL, *conn2 = NULL;
	int8_t rssi;
	int rc;

	common_init();
	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 5; i++) {
		/* Initiate connection */
		params.subscribe_commands = i % 2;
		params.subscribe_data = i % 2;
		params.subscribe_logging = i % 2;
		rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		/* Same connection again should pass with RC == 1 */
		rc = epacket_bt_gatt_connect(&conn2, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to detect existing connection\n");
			return;
		}
		bt_conn_unref(conn2);
		conn2 = NULL;

		/* Wait a little while */
		k_sleep(K_MSEC(200));
		/* Check the connection rssi */
		rssi = bt_conn_rssi(conn);
		if (rssi == 0) {
			FAIL("RSSI measurement not updated\n");
			return;
		}
		/* Terminate the connection */
		rc = bt_conn_disconnect_sync(conn);
		if (rc < 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	k_sleep(K_SECONDS(1));

	PASS("Gateway connect passed\n");
}

static void main_gateway_connect_multi(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn1 = NULL, *conn2 = NULL;
	bt_addr_le_t addr[2];
	int rc;

	common_init();
	if (observe_peers(addr, 2) < 0) {
		FAIL("Failed to observe peers\n");
		return;
	}

	for (int i = 0; i < 3; i++) {
		/* Connect to first device */
		params.peer = addr[0];
		rc = epacket_bt_gatt_connect(&conn1, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to connect to first peer\n");
			return;
		}

		/* Connect to the second device */
		params.peer = addr[1];
		rc = epacket_bt_gatt_connect(&conn2, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to connect to second peer %d\n", rc);
			return;
		}

		k_sleep(K_MSEC(500));

		/* Terminate the connections */
		rc = bt_conn_disconnect_sync(conn1);
		if (rc != 0) {
			FAIL("Failed to disconnect from first peer\n");
			return;
		}
		bt_conn_unref(conn1);
		conn1 = NULL;
		rc = bt_conn_disconnect_sync(conn2);
		if (rc != 0) {
			FAIL("Failed to disconnect from second peer\n");
			return;
		}
		bt_conn_unref(conn2);
		conn2 = NULL;
	}

	PASS("Received packets from advertiser\n");
}

static void main_gateway_connect_then_scan(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Initiate connection */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}

	/* Start scanning again */
	if (epacket_receive(epacket_bt_adv, K_FOREVER) < 0) {
		FAIL("Failed to resume scanning\n");
	}
	k_sleep(K_SECONDS(6));
	if (epacket_receive(epacket_bt_adv, K_NO_WAIT) < 0) {
		FAIL("Failed to terminate scanning\n");
	}

	/* Terminate the connection */
	rc = bt_conn_disconnect_sync(conn);
	if (rc < 0) {
		FAIL("Failed to disconnect from peer\n");
		return;
	}
	bt_conn_unref(conn);

	/* Missed the initial burst due to the connection */
	if (atomic_get(&received_packets) < 4) {
		FAIL("Failed to receive expected packets\n");
	} else {
		PASS("Received %d packets from advertiser\n", atomic_get(&received_packets));
	}
}

static void send_rpc(uint32_t request_id, uint16_t command_id, void *params, size_t params_len)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x0000,
	};
	struct infuse_rpc_req_header *h = params;

	h->command_id = command_id;
	h->request_id = request_id;

	/* Push command at RPC server */
	epacket_dummy_receive(epacket_dummy, &header, params, params_len);
}

static struct net_buf *expect_response(uint32_t request_id, uint16_t command_id, int rc)
{
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct infuse_rpc_rsp_header *response;
	struct epacket_dummy_frame *frame;
	struct net_buf *rsp;

	/* Response was sent */
	rsp = k_fifo_get(response_queue, K_SECONDS(10));
	if (rsp == NULL) {
		LOG_ERR("No response");
		return NULL;
	}
	frame = net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
	if (frame->type != INFUSE_RPC_RSP) {
		LOG_ERR("Unexpected response type (%d != %d)", INFUSE_RPC_RSP, frame->type);
		return NULL;
	}
	response = (void *)rsp->data;

	/* Parameters match what we expect */
	if (request_id != response->request_id) {
		LOG_ERR("Unexpected request ID (%08X != %08X)", response->request_id, request_id);
		return NULL;
	}
	if (rc != response->return_code) {
		LOG_ERR("Unexpected return code (%d != %d)", response->return_code, rc);
		return NULL;
	}

	/* Return the response */
	return rsp;
}

static void main_gateway_rpcs(void)
{
	struct rpc_bt_connect_infuse_response *connect_rsp;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	struct rpc_bt_connect_infuse_request connect = {
		.peer =
			{
				.type = addr.type,
				.val =
					{
						addr.a.val[0],
						addr.a.val[1],
						addr.a.val[2],
						addr.a.val[3],
						addr.a.val[4],
						addr.a.val[5],
					},
			},
		.conn_timeout_ms = 3000,
		.subscribe = 0,
		.inactivity_timeout_ms = 0,
	};
	struct rpc_bt_disconnect_request disconnect = {
		.peer = connect.peer,
	};

	/* Basic connect + disconnect cycle */
	send_rpc(1, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
	buf = expect_response(1, RPC_ID_BT_CONNECT_INFUSE, 0);
	if (buf == NULL) {
		FAIL("Failed to connect via RPC\n");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(2, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(2, RPC_ID_BT_DISCONNECT, 0);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result\n");
		return;
	}
	net_buf_unref(buf);

	/* Connect timeout, disconnect should error */
	connect.conn_timeout_ms = 10;
	send_rpc(3, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
	buf = expect_response(3, RPC_ID_BT_CONNECT_INFUSE, BT_HCI_ERR_UNKNOWN_CONN_ID);
	if (buf == NULL) {
		FAIL("Unexpected connection result\n");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(4, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(4, RPC_ID_BT_DISCONNECT, -EINVAL);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result\n");
		return;
	}
	net_buf_unref(buf);

	/* Connect with subscribe */
	connect.conn_timeout_ms = 3000;
	connect.subscribe =
		RPC_ENUM_INFUSE_BT_CHARACTERISTIC_COMMAND | RPC_ENUM_INFUSE_BT_CHARACTERISTIC_DATA;
	send_rpc(5, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
	buf = expect_response(5, RPC_ID_BT_CONNECT_INFUSE, 0);
	if (buf == NULL) {
		FAIL("Failed to connect via RPC\n");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(6, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(6, RPC_ID_BT_DISCONNECT, 0);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result\n");
		return;
	}
	net_buf_unref(buf);

	PASS("RPC connecter passed\n");
}

static K_FIFO_DEFINE(central_fifo);

void central_handler(struct net_buf *buf)
{
	k_fifo_put(&central_fifo, buf);
}

static void main_gateway_connect_recv(void)
{
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct epacket_rx_metadata *meta;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	int rc;

	common_init();
	epacket_set_receive_handler(epacket_central, central_handler);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 4; i++) {
		params.subscribe_data = i % 2;

		/* Connect to peer device */
		rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		if (params.subscribe_data) {
			/* Wait for a payload */
			buf = k_fifo_get(&central_fifo, K_SECONDS(2));
			if (buf == NULL) {
				FAIL("No packet received\n");
				return;
			}

			/* Validate metadata */
			meta = net_buf_user_data(buf);
			LOG_INF("Received %d bytes %d packet", buf->len, meta->type);
			if (meta->auth != EPACKET_AUTH_NETWORK) {
				FAIL("Unexpected authorisation (%d != %d)\n", meta->auth,
				     EPACKET_AUTH_NETWORK);
				return;
			}
			if (meta->type != INFUSE_TDF) {
				FAIL("Unexpected packet type (%d != %d)\n", meta->type, INFUSE_TDF);
				return;
			}
			net_buf_unref(buf);
		} else {
			/* Wait for a payload */
			buf = k_fifo_get(&central_fifo, K_MSEC(1500));
			if (buf != NULL) {
				FAIL("Unexpected packet received\n");
				return;
			}
		}

		/* Terminate the connections */
		rc = bt_conn_disconnect_sync(conn);
		if (rc != 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	PASS("Received TDF data from connected peer\n");
}

static void main_gateway_connect_idle_tx_timeout(void)
{
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_MSEC(1500),
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	union epacket_interface_address if_address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	struct rpc_application_info_request req;
	struct rpc_client_ctx ctx;

	if_address.bluetooth = params.peer;
	rpc_client_init(&ctx, epacket_central, if_address);

	/* Connect to peer device with an idle timeout.
	 * Don't subscribe to cmd responses by default to test the TX path.
	 */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;

	/* Connection should stay active while we're sending data */
	for (int i = 0; i < 5; i++) {
		/* Command will fail due to no response subscription, we don't care */
		(void)rpc_client_command_sync(&ctx, RPC_ID_APPLICATION_INFO, &req, sizeof(req),
					      K_NO_WAIT, K_MSEC(50), &buf);

		rc = k_sem_take(&bt_disconnected, K_MSEC(1000));
		if (rc == 0) {
			FAIL("Inactivity timer terminated despite transmissions\n");
			return;
		}
	}

	/* Connection should terminate once we stop */
	rc = k_sem_take(&bt_disconnected, K_MSEC(2000));
	if (rc != 0) {
		FAIL("Inactivity timer did not terminate connection\n");
		return;
	}
	k_sleep(K_MSEC(500));

	PASS("TX Inactivity timeout behaved as expected\n");
}

static void main_gateway_connect_idle_rx_timeout(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_MSEC(500),
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Connect to peer device with a timeout that we expect to expire */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;

	/* Expect the connection to disconnect within 1000 ms */
	rc = k_sem_take(&bt_disconnected, K_MSEC(1000));
	if (rc != 0) {
		FAIL("Inactivity timer did not terminate connection\n");
		return;
	}
	k_sleep(K_MSEC(500));

	/* Connect to peer device with a timeout that should not expire (peer sends at 1Hz) */
	params.inactivity_timeout = K_MSEC(1500);
	params.subscribe_data = true;
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);

	if (rc != 0) {
		FAIL("Failed to connect to peer %d\n", rc);
		return;
	}

	/* Validate no disconnection */
	rc = k_sem_take(&bt_disconnected, K_MSEC(5000));
	if (rc != -EAGAIN) {
		FAIL("Inactivity timer terminated unexpectedly\n");
		return;
	}

	/* Cleanup connection */
	rc = bt_conn_disconnect_sync(conn);
	if (rc != 0) {
		FAIL("Failed to disconnect from peer\n");
		return;
	}
	bt_conn_unref(conn);

	PASS("RX Inactivity timeout behaved as expected\n");
}

static void main_gateway_connect_idle_rx_log_ignored(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_MSEC(2000),
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = false,
		.subscribe_data = false,
		.subscribe_logging = true,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Connect to peer device with a long timeout, subscribed to logging characteristic */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;

	/* Expect the connection to disconnect within 2500 ms, since logging should not refesh
	 * the inactivity timeout.
	 */
	rc = k_sem_take(&bt_disconnected, K_MSEC(2500));
	if (rc != 0) {
		FAIL("Inactivity timer did not ignore logging data\n");
		return;
	}
	k_sleep(K_MSEC(500));

	PASS("RX Inactivity timeout ignored logging data\n");
}

static void main_gateway_connect_absolute_timeout(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_SECONDS(5),
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = true,
		.subscribe_logging = true,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Connect to peer device with a long timeout, subscribed to all characteristics */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;

	/* Expect the connection to terminate after 5 seconds regardless of the activity */
	rc = k_sem_take(&bt_disconnected, K_MSEC(4800));
	if (rc != -EAGAIN) {
		FAIL("Absolute timeout terminated early\n");
		return;
	}
	rc = k_sem_take(&bt_disconnected, K_MSEC(400));
	if (rc != 0) {
		FAIL("Absolute timer did not terminate connection at expected time\n");
		return;
	}
	k_sleep(K_MSEC(500));

	PASS("Absolute connection timeout terminated connection\n");
}

static void main_gateway_connect_absolute_timeout_update(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_SECONDS(1),
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = true,
		.subscribe_logging = true,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Connect to peer device with a long timeout, subscribed to all characteristics */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}
	bt_conn_unref(conn);
	conn = NULL;

	for (int i = 0; i < 6; i++) {
		rc = k_sem_take(&bt_disconnected, K_MSEC(500));
		if (rc != -EAGAIN) {
			FAIL("Connection terminated unexpectedly on iteration %d\n", i);
			return;
		}

		/* Refresh the connection */
		rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to refresh peer connection\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	/* Expect the connection to terminate after 5 seconds regardless of the activity */
	rc = k_sem_take(&bt_disconnected, K_MSEC(800));
	if (rc != -EAGAIN) {
		FAIL("Absolute timeout terminated early\n");
		return;
	}
	rc = k_sem_take(&bt_disconnected, K_MSEC(400));
	if (rc != 0) {
		FAIL("Absolute timer did not terminate connection at expected time\n");
		return;
	}
	k_sleep(K_MSEC(500));

	PASS("Absolute connection timeout updates each call\n");
}

static void main_gateway_connect_absolute_timeout_cancel(void)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_SECONDS(3),
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = true,
		.subscribe_logging = true,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	int rc;

	common_init();
	bt_conn_cb_register(&conn_cb);

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Connect to peer device with a long timeout, subscribed to all characteristics*/
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		FAIL("Failed to connect to peer\n");
		return;
	}

	/* Sleep a short duration */
	k_sleep(K_MSEC(500));

	/* Terminate the connection */
	rc = bt_conn_disconnect_sync(conn);
	if (rc != 0) {
		FAIL("Failed to disconnect\n");
		return;
	}
	bt_conn_unref(conn);

	/* Wait until after the connection would normally terminate */
	k_sleep(K_SECONDS(3));

	PASS("Absolute connection timeout cleaned up\n");
}

static void main_gateway_remote_rpc_client(void)
{
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 3000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	union epacket_interface_address address, wrong;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	int rc;

	struct rpc_application_info_request req;
	struct rpc_application_info_response *rsp;
	struct rpc_client_ctx ctx;

	common_init();

	if (observe_peers(&params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}
	address.bluetooth = params.peer;
	rpc_client_init(&ctx, epacket_central, address);

	for (int i = 0; i < 4; i++) {
		/* Connect to peer device */
		rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		/* Send to incorrect device */
		wrong.bluetooth = params.peer;
		wrong.bluetooth.a.val[0] += 1;
		for (int i = 0; i < 5; i++) {
			buf = epacket_alloc_tx_for_interface(epacket_central, K_MSEC(1));
			if (buf == NULL) {
				FAIL("Failed to allocate buffer\n");
				return;
			}
			epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0, INFUSE_KEY_IDS,
						wrong);
			epacket_queue(epacket_central, buf);
		}

		/* Run a command on the peer device */
		rc = rpc_client_command_sync(&ctx, RPC_ID_APPLICATION_INFO, &req, sizeof(req),
					     K_NO_WAIT, K_MSEC(200), &buf);
		if (rc < 0) {
			FAIL("Failed to query version (%d)\n", rc);
			return;
		}
		rsp = (void *)buf->data;
		LOG_INF("Application: %08X", rsp->application_id);
		LOG_INF("    Version: %d.%d.%d+%08x", rsp->version.major, rsp->version.minor,
			rsp->version.revision, rsp->version.build_num);
		LOG_INF("     Uptime: %d", rsp->uptime);
		net_buf_unref(buf);

		/* Terminate the connections */
		rc = bt_conn_disconnect_sync(conn);
		if (rc != 0) {
			FAIL("Failed to disconnect from peer\n");
			return;
		}
		bt_conn_unref(conn);
		conn = NULL;
	}

	/* Unregister from callbacks */
	rpc_client_cleanup(&ctx);

	PASS("Ran commands on peer\n");
}

static void dummy_gateway_handler(struct net_buf *buf)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_gateway_receive_handler(epacket_dummy, buf);
}

static struct net_buf *create_rpc_request(const struct device *interface, void *request,
					  size_t request_len)
{
	struct net_buf *buf;

	buf = epacket_alloc_tx_for_interface(interface, K_FOREVER);
	epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0, INFUSE_RPC_CMD, EPACKET_ADDR_ALL);
	net_buf_add_mem(buf, request, request_len);
	if (epacket_bt_gatt_encrypt(buf, infuse_security_network_key_identifier()) < 0) {
		FAIL("Failed to encrypt GATT RPC\n");
		net_buf_unref(buf);
		return NULL;
	}
	return buf;
}

static struct net_buf *create_info_request(const struct device *interface,
					   struct rpc_application_info_request *request)
{
	return create_rpc_request(interface, request, sizeof(*request));
}

static int check_info_response(struct net_buf *buf, struct rpc_application_info_request *request)
{
	struct epacket_dummy_frame *frame;
	struct epacket_received_common_header *common_header;
	struct epacket_received_decrypted_header *decr_header;
	struct rpc_application_info_response *info_rsp;

	frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
	if (frame->type != INFUSE_RECEIVED_EPACKET) {
		FAIL("Unexpected packet type\n");
		return -1;
	}
	common_header = net_buf_pull_mem(buf, sizeof(struct epacket_received_common_header));
	if (common_header->interface != EPACKET_INTERFACE_BT_CENTRAL) {
		FAIL("Unexpected interface\n");
		return -1;
	}
	net_buf_pull_mem(buf, sizeof(struct epacket_interface_address_bt_le));
	decr_header = net_buf_pull_mem(buf, sizeof(struct epacket_received_decrypted_header));
	if (decr_header->type != INFUSE_RPC_RSP) {
		FAIL("Unexpected packet type\n");
		return -1;
	}

	info_rsp = (void *)buf->data;
	if (info_rsp->header.request_id != request->header.request_id) {
		FAIL("Unexpected request ID\n");
		return -1;
	}
	if (info_rsp->header.command_id != request->header.command_id) {
		FAIL("Unexpected command ID\n");
		return -1;
	}
	if (info_rsp->header.return_code != 0) {
		FAIL("Unexpected return code\n");
		return -1;
	}
	/* This only works because both devices have the same timebase due to the
	 * simulation
	 */
	if (info_rsp->uptime != k_uptime_seconds()) {
		FAIL("Unexpected uptime\n");
		return -1;
	}
	return 0;
}

static void main_gateway_remote_rpc_forward(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_bt_connect_infuse_response *connect_rsp;
	union epacket_interface_address address;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	struct rpc_bt_connect_infuse_request connect = {
		.peer =
			{
				.type = addr.type,
				.val =
					{
						addr.a.val[0],
						addr.a.val[1],
						addr.a.val[2],
						addr.a.val[3],
						addr.a.val[4],
						addr.a.val[5],
					},
			},
		.conn_timeout_ms = 3000,
		.subscribe = RPC_ENUM_INFUSE_BT_CHARACTERISTIC_COMMAND,
		.inactivity_timeout_ms = 0,
	};
	struct rpc_bt_disconnect_request disconnect = {
		.peer = connect.peer,
	};

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	for (int i = 0; i < 3; i++) {
		/* Connect to the remote device */
		send_rpc(1, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
		buf = expect_response(1, RPC_ID_BT_CONNECT_INFUSE, 0);
		if (buf == NULL) {
			FAIL("Failed to connect via RPC\n");
			return;
		}
		connect_rsp = (void *)buf->data;
		net_buf_unref(buf);

		/* Create and encrypt the GATT RPC */
		struct rpc_application_info_request info_request = {
			.header.command_id = RPC_ID_APPLICATION_INFO,
			.header.request_id = 0x12345678,
		};

		address.bluetooth = addr;
		buf = create_info_request(epacket_central, &info_request);
		if (buf == NULL) {
			return;
		}

		/* Construct ePacket forwarding packet */
		struct epacket_dummy_frame dummy_header = {
			.type = INFUSE_EPACKET_FORWARD,
			.auth = EPACKET_AUTH_DEVICE,
		};
		struct forwarding_bt {
			struct epacket_forward_header forward_header;
			uint8_t bt_addr[7];
		} __packed hdr;

		hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
		hdr.forward_header.length = sizeof(hdr) + buf->len;
		hdr.bt_addr[0] = addr.type;
		memcpy(hdr.bt_addr + 1, addr.a.val, 6);

		/* Push packet at dummy interface */
		epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr),
					    buf->data, buf->len);
		net_buf_unref(buf);

		/* Expect response to appear on the epacket output */
		buf = k_fifo_get(response_queue, K_SECONDS(1));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return;
		}
		if (check_info_response(buf, &info_request) < 0) {
			return;
		}
		net_buf_unref(buf);

		/* Disconnect from the remote device */
		send_rpc(2, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
		buf = expect_response(2, RPC_ID_BT_DISCONNECT, 0);
		if (buf == NULL) {
			FAIL("Unexpected disconnection result\n");
			return;
		}
		net_buf_unref(buf);
	}

	PASS("RPC forwarder passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Run the process several times */
	for (int i = 0; i < 3; i++) {
		/* Create and encrypt the GATT RPC */
		struct rpc_application_info_request info_request = {
			.header.command_id = RPC_ID_APPLICATION_INFO,
			.header.request_id = 0xAA345678,
		};

		address.bluetooth = addr;
		buf = create_info_request(epacket_central, &info_request);
		if (buf == NULL) {
			return;
		}

		/* Construct ePacket forwarding packet */
		struct epacket_dummy_frame dummy_header = {
			.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
			.auth = EPACKET_AUTH_DEVICE,
		};
		struct forwarding_bt {
			struct epacket_forward_auto_conn_header forward_header;
			uint8_t bt_addr[7];
		} __packed hdr;

		hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
		hdr.forward_header.length = sizeof(hdr) + buf->len;
		hdr.forward_header.flags = 0;
		hdr.forward_header.conn_timeout = 2;
		hdr.forward_header.conn_idle_timeout = 1;
		hdr.forward_header.conn_absolute_timeout = 5;
		hdr.bt_addr[0] = addr.type;
		memcpy(hdr.bt_addr + 1, addr.a.val, 6);

		/* Push packet at dummy interface */
		epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr),
					    buf->data, buf->len);
		net_buf_unref(buf);

		/* Expect response to appear on the epacket output */
		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return;
		}
		if (check_info_response(buf, &info_request) < 0) {
			return;
		}
		net_buf_unref(buf);

		/* There should be a connection associated with the peer */
		conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &addr);
		if (conn == NULL) {
			FAIL("Could not find associated connection\n");
			return;
		}

		/* Connection should disconnect due to idle timeout */
		if (bt_conn_disconnect_wait(conn, K_SECONDS(2)) < 0) {
			FAIL("Connection did not terminate\n");
			return;
		}
		bt_conn_unref(conn);

		/* Small delay before next iteration */
		k_sleep(K_MSEC(10));
	}
	PASS("RPC auto-conn forwarder passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn_single(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Run the process several times */
	for (int i = 0; i < 3; i++) {
		/* Create and encrypt the GATT RPC */
		struct rpc_application_info_request info_request = {
			.header.command_id = RPC_ID_APPLICATION_INFO,
			.header.request_id = 0xAA345678,
		};

		address.bluetooth = addr;
		buf = create_info_request(epacket_central, &info_request);
		if (buf == NULL) {
			return;
		}

		/* Construct ePacket forwarding packet */
		struct epacket_dummy_frame dummy_header = {
			.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
			.auth = EPACKET_AUTH_DEVICE,
		};
		struct forwarding_bt {
			struct epacket_forward_auto_conn_header forward_header;
			uint8_t bt_addr[7];
		} __packed hdr;

		hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
		hdr.forward_header.length = sizeof(hdr) + buf->len;
		hdr.forward_header.flags = EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC;
		hdr.forward_header.conn_timeout = 2;
		hdr.forward_header.conn_idle_timeout = 5;
		hdr.forward_header.conn_absolute_timeout = 5;
		hdr.bt_addr[0] = addr.type;
		memcpy(hdr.bt_addr + 1, addr.a.val, 6);

		/* Push packet at dummy interface */
		epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr),
					    buf->data, buf->len);
		net_buf_unref(buf);

		/* Expect response to appear on the epacket output */
		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return;
		}
		if (check_info_response(buf, &info_request) < 0) {
			return;
		}
		net_buf_unref(buf);

		/* Give a short duration to allow for connection cleanup*/
		k_sleep(K_MSEC(50));

		/* The connection should have been automatically terminated on the RPC_RSP */
		conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &addr);
		if (conn != NULL) {
			FAIL("Connection associated with one-shot RPC still active\n");
			return;
		}
	}
	PASS("RPC auto-conn forwarder passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn_dc_notify(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Run the process several times */
	for (int i = 0; i < 3; i++) {
		/* Create and encrypt the GATT RPC */
		struct rpc_application_info_request info_request = {
			.header.command_id = RPC_ID_APPLICATION_INFO,
			.header.request_id = 0xAA345678,
		};

		address.bluetooth = addr;
		buf = create_info_request(epacket_central, &info_request);
		if (buf == NULL) {
			return;
		}

		/* Construct ePacket forwarding packet */
		struct epacket_dummy_frame dummy_header = {
			.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
			.auth = EPACKET_AUTH_DEVICE,
		};
		struct forwarding_bt {
			struct epacket_forward_auto_conn_header forward_header;
			uint8_t bt_addr[7];
		} __packed hdr;

		hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
		hdr.forward_header.length = sizeof(hdr) + buf->len;
		hdr.forward_header.flags = EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC |
					   EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION;
		hdr.forward_header.conn_timeout = 2;
		hdr.forward_header.conn_idle_timeout = 5;
		hdr.forward_header.conn_absolute_timeout = 5;
		hdr.bt_addr[0] = addr.type;
		memcpy(hdr.bt_addr + 1, addr.a.val, 6);

		/* Push packet at dummy interface */
		epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr),
					    buf->data, buf->len);
		net_buf_unref(buf);

		/* Expect response to appear on the epacket output */
		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return;
		}
		if (check_info_response(buf, &info_request) < 0) {
			return;
		}
		net_buf_unref(buf);

		/* Give a short duration to allow for connection cleanup*/
		k_sleep(K_MSEC(50));

		/* The connection should have been automatically terminated on the RPC_RSP */
		conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &addr);
		if (conn != NULL) {
			FAIL("Connection associated with one-shot RPC still active\n");
			return;
		}

		/* Expect a INFUSE_EPACKET_CONN_TERMINATED packet */
		struct epacket_dummy_frame *dummy_header_tx;
		struct epacket_conn_terminated *terminated;
		struct epacket_interface_address_bt_le *terminated_addr;

		buf = k_fifo_get(response_queue, K_SECONDS(1));
		if (buf == NULL) {
			FAIL("Failed to see INFUSE_EPACKET_CONN_TERMINATED\n");
			return;
		}

		dummy_header_tx = net_buf_pull_mem(buf, sizeof(*dummy_header_tx));
		terminated = net_buf_pull_mem(buf, sizeof(*terminated));
		terminated_addr = net_buf_pull_mem(buf, sizeof(*terminated_addr));
		if (dummy_header_tx->type != INFUSE_EPACKET_CONN_TERMINATED) {
			FAIL("Packet is not INFUSE_EPACKET_CONN_TERMINATED\n");
			return;
		}
		if (dummy_header_tx->auth != EPACKET_AUTH_DEVICE) {
			FAIL("Unexpected auth\n");
			return;
		}
		if (terminated->interface != EPACKET_INTERFACE_BT_CENTRAL) {
			FAIL("Unexpected interface\n");
			return;
		}
		if (terminated->reason != BT_HCI_ERR_LOCALHOST_TERM_CONN) {
			FAIL("Unexpected reason\n");
			return;
		}
		if (terminated_addr->type != addr.type) {
			FAIL("Unexpected interface address type\n");
			return;
		}
		if (memcmp(terminated_addr->addr, addr.a.val, 6) != 0) {
			FAIL("Unexpected interface address value\n");
			return;
		}
		net_buf_unref(buf);
	}
	PASS("RPC auto-conn forwarder INFUSE_EPACKET_CONN_TERMINATED passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn_fail(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Create and encrypt the GATT RPC */
	struct rpc_application_info_request info_request = {
		.header.command_id = RPC_ID_APPLICATION_INFO,
		.header.request_id = 0xAA345678,
	};

	address.bluetooth = addr;
	buf = create_info_request(epacket_central, &info_request);
	if (buf == NULL) {
		return;
	}

	/* Construct ePacket forwarding packet */
	struct epacket_dummy_frame dummy_header = {
		.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct forwarding_bt {
		struct epacket_forward_auto_conn_header forward_header;
		uint8_t bt_addr[7];
	} __packed hdr;

	/* Change address to be incorrect */
	addr.a.val[0] += 1;

	hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
	hdr.forward_header.length = sizeof(hdr) + buf->len;
	hdr.forward_header.flags = EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION;
	hdr.forward_header.conn_timeout = 2;
	hdr.forward_header.conn_idle_timeout = 5;
	hdr.forward_header.conn_absolute_timeout = 5;
	hdr.bt_addr[0] = addr.type;
	memcpy(hdr.bt_addr + 1, addr.a.val, 6);

	/* Push packet at dummy interface */
	epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr), buf->data,
				    buf->len);
	net_buf_unref(buf);

	/* Expect a INFUSE_EPACKET_CONN_TERMINATED packet */
	struct epacket_dummy_frame *dummy_header_tx;
	struct epacket_conn_terminated *terminated;
	struct epacket_interface_address_bt_le *terminated_addr;

	buf = k_fifo_get(response_queue, K_SECONDS(3));
	if (buf == NULL) {
		FAIL("Failed to see INFUSE_EPACKET_CONN_TERMINATED\n");
		return;
	}

	dummy_header_tx = net_buf_pull_mem(buf, sizeof(*dummy_header_tx));
	terminated = net_buf_pull_mem(buf, sizeof(*terminated));
	terminated_addr = net_buf_pull_mem(buf, sizeof(*terminated_addr));
	if (dummy_header_tx->type != INFUSE_EPACKET_CONN_TERMINATED) {
		FAIL("Packet is not INFUSE_EPACKET_CONN_TERMINATED\n");
		return;
	}
	if (dummy_header_tx->auth != EPACKET_AUTH_DEVICE) {
		FAIL("Unexpected auth\n");
		return;
	}
	if (terminated->interface != EPACKET_INTERFACE_BT_CENTRAL) {
		FAIL("Unexpected interface\n");
		return;
	}
	printk("REASON %d\n", terminated->reason);
	if (terminated->reason != BT_HCI_ERR_UNKNOWN_CONN_ID) {
		FAIL("Unexpected reason\n");
		return;
	}
	if (terminated_addr->type != addr.type) {
		FAIL("Unexpected interface address type\n");
		return;
	}
	if (memcmp(terminated_addr->addr, addr.a.val, 6) != 0) {
		FAIL("Unexpected interface address value\n");
		return;
	}
	net_buf_unref(buf);
	PASS("RPC auto-conn forwarder INFUSE_EPACKET_CONN_TERMINATED passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn_auth_fail(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Create and encrypt the GATT RPC */
	struct rpc_application_info_request info_request = {
		.header.command_id = RPC_ID_APPLICATION_INFO,
		.header.request_id = 0xAA345678,
	};

	address.bluetooth = addr;
	buf = create_info_request(epacket_central, &info_request);
	if (buf == NULL) {
		return;
	}

	/* Construct ePacket forwarding packet */
	struct epacket_dummy_frame dummy_header = {
		.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
		.auth = EPACKET_AUTH_FAILURE,
	};
	struct forwarding_bt {
		struct epacket_forward_auto_conn_header forward_header;
		uint8_t bt_addr[7];
	} __packed hdr;

	hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
	hdr.forward_header.length = sizeof(hdr) + buf->len;
	hdr.forward_header.flags = EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION;
	hdr.forward_header.conn_timeout = 2;
	hdr.forward_header.conn_idle_timeout = 5;
	hdr.forward_header.conn_absolute_timeout = 5;
	hdr.bt_addr[0] = addr.type;
	memcpy(hdr.bt_addr + 1, addr.a.val, 6);

	/* Push this packet many times */
	for (int i = 0; i < 10; i++) {
		epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr),
					    buf->data, buf->len);
	}
	net_buf_unref(buf);

	/* Expect no connection terminated because no connections should have been created */
	buf = k_fifo_get(response_queue, K_SECONDS(3));
	if (buf != NULL) {
		FAIL("Connection unexpectedly established\n");
		return;
	}

	PASS("RPC auto-conn forwarder with auth failures passed\n");
}

static int run_data_sender(bt_addr_le_t *addr, uint16_t rpc, uint32_t size, bool slow_uplink,
			   bool prioritise)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *frame;
	struct epacket_received_common_header *common_header;
	struct epacket_received_decrypted_header *decr_header;
	struct infuse_rpc_data *data_header;
	struct rpc_data_sender_response *sender_rsp;
	union epacket_interface_address address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	uint32_t expected_offset = 0;
	uint16_t data_len;
	int32_t start_time, duration;
	uint32_t request_id = 0xBB345678;
	uint32_t total_data_len;

	if (rpc == RPC_ID_DATA_SENDER) {
		/* Create and encrypt the GATT RPC */
		struct rpc_data_sender_request sender_request = {
			.header =
				{
					.command_id = RPC_ID_DATA_SENDER,
					.request_id = request_id,
				},
			.data_header =
				{
					.size = size,
					.rx_ack_period = 0,
				},

		};
		buf = create_rpc_request(epacket_central, &sender_request, sizeof(sender_request));
		total_data_len = sender_request.data_header.size;
	} else if (rpc == RPC_ID_DATA_LOGGER_READ) {
		/* Create and encrypt the GATT RPC */
		struct rpc_data_logger_read_request data_logger_read_request = {
			.header =
				{
					.command_id = RPC_ID_DATA_LOGGER_READ,
					.request_id = request_id,
				},
			.data_header =
				{
					.size = size,
					.rx_ack_period = 0,
				},
			.logger = RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD,
			.start_block = 0,
			.last_block = (size / 512) - 1,

		};
		buf = create_rpc_request(epacket_central, &data_logger_read_request,
					 sizeof(data_logger_read_request));
		total_data_len = data_logger_read_request.data_header.size;
	} else {
		FAIL("Unimplemented RPC %d\n", rpc);
		return -1;
	}
	if (buf == NULL) {
		FAIL("Failed to allocate request\n");
		return -1;
	}

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	address.bluetooth = *addr;

	/* Construct ePacket forwarding packet */
	struct epacket_dummy_frame dummy_header = {
		.type = INFUSE_EPACKET_FORWARD_AUTO_CONN,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct forwarding_bt {
		struct epacket_forward_auto_conn_header forward_header;
		uint8_t bt_addr[7];
	} __packed hdr;

	hdr.forward_header.interface = EPACKET_INTERFACE_BT_CENTRAL;
	hdr.forward_header.length = sizeof(hdr) + buf->len;
	hdr.forward_header.flags = EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC |
				   (prioritise ? EPACKET_FORWARD_AUTO_CONN_PRIORITISE_UPLINK : 0);
	hdr.forward_header.conn_timeout = 2;
	hdr.forward_header.conn_idle_timeout = 5;
	hdr.forward_header.conn_absolute_timeout = 7;
	hdr.bt_addr[0] = addr->type;
	memcpy(hdr.bt_addr + 1, addr->a.val, 6);

	/* Push packet at dummy interface */
	epacket_dummy_receive_extra(epacket_dummy, &dummy_header, &hdr, sizeof(hdr), buf->data,
				    buf->len);
	net_buf_unref(buf);

	start_time = k_uptime_get_32();

	while (expected_offset != total_data_len) {
		printk("%d %d\n", expect_response, total_data_len);
		if (slow_uplink) {
			/* Free transmit buffers very slowly.
			 * Without rate limiting, this would fail with dropped buffers.
			 */
#ifdef CONFIG_EPACKET_RECEIVE_GROUPING
			k_sleep(K_MSEC(100));
#else
			k_sleep(K_MSEC(50));
#endif
		}

		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return -1;
		}

		frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
		if (frame->type != INFUSE_RECEIVED_EPACKET) {
			FAIL("Unexpected packet type\n");
			return -1;
		}
		/* Consume all grouped packets */
		while (buf->len) {
			if (prioritise != infuse_state_get(INFUSE_STATE_HIGH_PRIORITY_UPLINK)) {
				FAIL("Unexpected INFUSE_STATE_HIGH_PRIORITY_UPLINK state\n");
				return 1;
			}
			common_header = net_buf_pull_mem(
				buf, sizeof(struct epacket_received_common_header));
			if (common_header->interface != EPACKET_INTERFACE_BT_CENTRAL) {
				FAIL("Unexpected interface\n");
				return -1;
			}
			net_buf_pull_mem(buf, sizeof(struct epacket_interface_address_bt_le));
			decr_header = net_buf_pull_mem(
				buf, sizeof(struct epacket_received_decrypted_header));
			if (decr_header->type != INFUSE_RPC_DATA) {
				FAIL("Unexpected packet type\n");
				return -1;
			}
			data_header = net_buf_pull_mem(buf, sizeof(struct infuse_rpc_data));
			if (data_header->request_id != request_id) {
				FAIL("Unexpected request ID\n");
				return -1;
			}
			if (data_header->offset != expected_offset) {
				FAIL("Unexpected data offset\n");
				return -1;
			}
			data_len = common_header->len_encrypted - sizeof(*common_header) -
				   sizeof(struct epacket_interface_address_bt_le) -
				   sizeof(*decr_header) - sizeof(*data_header);
			net_buf_pull_mem(buf, data_len);

			expected_offset += data_len;

			if (expected_offset == total_data_len) {
				/* Data transfer complete */
				break;
			}
		}
		if (buf->len == 0) {
			net_buf_unref(buf);
			buf = NULL;
		}
	}

	if (buf == NULL) {
		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive final response\n");
			return -1;
		}
		frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
		if (frame->type != INFUSE_RECEIVED_EPACKET) {
			FAIL("Unexpected final response packet type\n");
			return -1;
		}
	}

	duration = k_uptime_get_32() - start_time;

	/* Expect the final RPC_RSP to be present as the last payload */
	common_header = net_buf_pull_mem(buf, sizeof(struct epacket_received_common_header));
	if (common_header->interface != EPACKET_INTERFACE_BT_CENTRAL) {
		FAIL("Unexpected interface\n");
		return -1;
	}
	net_buf_pull_mem(buf, sizeof(struct epacket_interface_address_bt_le));
	decr_header = net_buf_pull_mem(buf, sizeof(struct epacket_received_decrypted_header));
	if (decr_header->type != INFUSE_RPC_RSP) {
		FAIL("Unexpected packet type\n");
		return -1;
	}
	sender_rsp = net_buf_pull_mem(buf, sizeof(*sender_rsp));
	if (sender_rsp->header.request_id != request_id) {
		FAIL("Unexpected RPC_RSP request ID\n");
		return -1;
	}
	if (sender_rsp->header.command_id != rpc) {
		FAIL("Unexpected RPC_RSP command ID %d %d\n", sender_rsp->header.command_id, rpc);
		return -1;
	}
	if (sender_rsp->header.return_code != 0) {
		FAIL("Unexpected RPC_RSP return code\n");
		return -1;
	}
	net_buf_unref(buf);

	/* Give a short duration to allow for connection cleanup */
	k_sleep(K_MSEC(50));

	/* The connection should have been automatically terminated on the RPC_RSP */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);
	if (conn != NULL) {
		FAIL("Connection associated with one-shot RPC still active\n");
		return -1;
	}
	return duration;
}

static void main_gateway_remote_rpc_forward_auto_conn_rate_limit(void)
{
	bt_addr_le_t addr;

	(void)kv_store_delete(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT);

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Run the receiving process for 8kB with a slow uplink */
	if (run_data_sender(&addr, RPC_ID_DATA_SENDER, 8192, true, false) < 0) {
		return;
	}
	PASS("RPC auto-conn forwarder with delay based rate-limiting passed\n");
}

static void main_gateway_remote_rpc_forward_auto_conn_rate_throughput(void)
{
	bt_addr_le_t addr;
	int duration;

	struct kv_bluetooth_throughput_limit limit = {
		.limit_kbps = 8,
	};

	if (KV_STORE_WRITE(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT, &limit) != sizeof(limit)) {
		FAIL("Failed to write throughput limit\n");
		return;
	}

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Run the receiving process for 4kB with a 8kbps limit */
	duration = run_data_sender(&addr, RPC_ID_DATA_SENDER, 4096, false, true);
	if (duration < 0) {
		return;
	}
	/* Expect this to take between 4 and 5 seconds:
	 *    4 seconds for the data transfer
	 *  0-1 seconds for the connection
	 */
	if ((duration < 4000) || (duration > 5000)) {
		FAIL("Unexpected connection duration (%d ms)", duration);
	}
	PASS("RPC auto-conn forwarder with throughput based rate-limiting passed (%d ms)\n",
	     duration);
}

static void main_gateway_data_logger_read_throughput(void)
{
	bt_addr_le_t addr;
	int duration;

	struct kv_bluetooth_throughput_limit limit = {
		.limit_kbps = 8,
	};

	if (KV_STORE_WRITE(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT, &limit) != sizeof(limit)) {
		FAIL("Failed to write throughput limit\n");
		return;
	}

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	/* Run the data logger read for 4kB with a 8kbps limit */
	duration = run_data_sender(&addr, RPC_ID_DATA_LOGGER_READ, 4096, false, false);
	if (duration < 0) {
		return;
	}
	/* Expect this to take between 4 and 5 seconds:
	 *    4 seconds for the data transfer
	 *  0-1 seconds for the connection
	 */
	if ((duration < 4000) || (duration > 5000)) {
		FAIL("Unexpected connection duration (%d ms)", duration);
	}
	PASS("Data logger read passed (%d ms)\n", duration);
}

static void main_gateway_bt_file_copy(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_bt_gatt_connect_params conn_params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = 2000,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = false,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	union epacket_interface_address address;
	struct bt_conn *conn = NULL;
	struct net_buf *buf;
	uint32_t flash_crc;
	int rc;

	common_init();
	if (observe_peers(&conn_params.peer, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}
	address.bluetooth = conn_params.peer;

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	/* Write random data to the file_partition */
	const struct flash_area *fa;

	flash_area_open(FIXED_PARTITION_ID(file_partition), &fa);
	sys_rand_get(mem_buffer, sizeof(mem_buffer));
	for (int i = 0; i < 8096; i += sizeof(mem_buffer)) {
		flash_area_write(fa, i, mem_buffer, sizeof(mem_buffer));
	}

	/* Command requires a connection to the device to already exist */
	struct rpc_bt_file_copy_basic_request file_copy_request = {
		.header.command_id = RPC_ID_BT_FILE_COPY_BASIC,
		.header.request_id = 0xCC345678,
		.peer =
			{
				.type = conn_params.peer.type,
				.val =
					{
						conn_params.peer.a.val[0],
						conn_params.peer.a.val[1],
						conn_params.peer.a.val[2],
						conn_params.peer.a.val[3],
						conn_params.peer.a.val[4],
						conn_params.peer.a.val[5],
					},
			},
		/* FILE_FOR_COPY to simulate flash write times */
		.action = RPC_ENUM_FILE_ACTION_FILE_FOR_COPY,
		.file_idx = 0,
		.file_len = 4123,
		.ack_period = 1,
		.pipelining = 0,
	};
	struct epacket_dummy_frame dummy_header = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
	};
	struct infuse_rpc_rsp_header *rpc_rsp;
	struct epacket_dummy_frame *frame;

	flash_area_crc32(fa, 0, file_copy_request.file_len, &flash_crc, mem_buffer,
			 sizeof(mem_buffer));
	file_copy_request.file_crc = flash_crc;

	/* Push packet at dummy interface */
	epacket_dummy_receive(epacket_dummy, &dummy_header, &file_copy_request,
			      sizeof(file_copy_request));

	/* Expect error response to appear on the epacket output */
	buf = k_fifo_get(response_queue, K_SECONDS(1));
	if (buf == NULL) {
		FAIL("Failed to receive response\n");
		return;
	}
	frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
	if (frame->type != INFUSE_RPC_RSP) {
		FAIL("Unexpected packet type\n");
		return;
	}
	rpc_rsp = net_buf_pull_mem(buf, sizeof(struct infuse_rpc_rsp_header));
	if (rpc_rsp->command_id != RPC_ID_BT_FILE_COPY_BASIC) {
		FAIL("Unexpected command ID %d\n", rpc_rsp->command_id);
		return;
	}
	if (rpc_rsp->return_code != -ENOTCONN) {
		FAIL("Unexpected command return code\n");
		return;
	}
	net_buf_unref(buf);

	/* Run the process several times */
	for (int i = 0; i < 4; i++) {
		file_copy_request.header.request_id += 1;
		file_copy_request.file_len += 1;
		flash_area_crc32(fa, 0, file_copy_request.file_len, &flash_crc, mem_buffer,
				 sizeof(mem_buffer));
		file_copy_request.file_crc = flash_crc;
		file_copy_request.ack_period = (i > 2) ? i - 1 : 1;
		file_copy_request.pipelining = (i > 0) ? 2 : 0;

		/* Create the Bluetooth connection */
		rc = epacket_bt_gatt_connect(&conn, &conn_params, &security_info);
		if (rc != 0) {
			FAIL("Failed to create connection\n");
			return;
		}

		/* Push packet at dummy interface */
		epacket_dummy_receive(epacket_dummy, &dummy_header, &file_copy_request,
				      sizeof(file_copy_request));

		/* Expect response to appear on the epacket output */
		buf = k_fifo_get(response_queue, K_SECONDS(2));
		if (buf == NULL) {
			FAIL("Failed to receive response\n");
			return;
		}
		frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
		if (frame->type != INFUSE_RPC_RSP) {
			FAIL("Unexpected packet type %d\n", frame->type);
			return;
		}
		rpc_rsp = net_buf_pull_mem(buf, sizeof(struct infuse_rpc_rsp_header));
		if (rpc_rsp->command_id != RPC_ID_BT_FILE_COPY_BASIC) {
			FAIL("Unexpected command ID %d\n", rpc_rsp->command_id);
			return;
		}
		if (rpc_rsp->return_code != 0) {
			FAIL("Unexpected command return code\n");
			return;
		}
		net_buf_unref(buf);

		rc = bt_conn_disconnect_sync(conn);
		bt_conn_unref(conn);
		if (rc != 0) {
			FAIL("Failed to disconnect\n");
			return;
		}

		/* Give a short duration to allow for connection cleanup*/
		k_sleep(K_MSEC(50));
	}

	flash_area_close(fa);

	PASS("BT file copy passed\n");
}

static void main_gateway_mcumgr_reboot(void)
{
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	struct rpc_bt_mcumgr_reboot_request connect = {
		.peer =
			{
				.type = addr.type,
				.val =
					{
						addr.a.val[0],
						addr.a.val[1],
						addr.a.val[2],
						addr.a.val[3],
						addr.a.val[4],
						addr.a.val[5],
					},
			},
		.conn_timeout_ms = 2000,
	};

	/* Non-existent device */
	connect.peer.val[0] += 1;
	send_rpc(500, RPC_ID_BT_MCUMGR_REBOOT, &connect, sizeof(connect));
	buf = expect_response(500, RPC_ID_BT_MCUMGR_REBOOT, BT_HCI_ERR_UNKNOWN_CONN_ID);
	if (buf == NULL) {
		FAIL("Failed to connect via RPC\n");
		return;
	}
	net_buf_unref(buf);

	/* Device that exists */
	connect.peer.val[0] -= 1;
	send_rpc(1000, RPC_ID_BT_MCUMGR_REBOOT, &connect, sizeof(connect));
	buf = expect_response(1000, RPC_ID_BT_MCUMGR_REBOOT, 0);
	if (buf == NULL) {
		FAIL("Failed to connect via RPC\n");
		return;
	}
	net_buf_unref(buf);

	k_sleep(K_TIMEOUT_ABS_SEC(9));

	PASS("MCUMGR rebooter passed\n");
}

static void main_gateway_mcumgr_none_reboot(void)
{
	struct net_buf *buf;
	bt_addr_le_t addr;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	struct rpc_bt_mcumgr_reboot_request connect = {
		.peer =
			{
				.type = addr.type,
				.val =
					{
						addr.a.val[0],
						addr.a.val[1],
						addr.a.val[2],
						addr.a.val[3],
						addr.a.val[4],
						addr.a.val[5],
					},
			},
		.conn_timeout_ms = 2000,
	};

	/* Device exists, but no MCUMGR characteristic */
	send_rpc(600, RPC_ID_BT_MCUMGR_REBOOT, &connect, sizeof(connect));
	buf = expect_response(600, RPC_ID_BT_MCUMGR_REBOOT, -EBADF);
	if (buf == NULL) {
		FAIL("Failed to connect via RPC\n");
		return;
	}
	net_buf_unref(buf);

	k_sleep(K_TIMEOUT_ABS_SEC(9));

	PASS("MCUMGR NONE rebooter passed\n");
}

static const struct bst_test_instance epacket_gateway[] = {
	{
		.test_id = "epacket_bt_gateway_scan",
		.test_descr = "Scans for advertising ePackets on advertising PHY",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_scan,
	},
	{
		.test_id = "epacket_bt_gateway_scan_wdog",
		.test_descr = "Check Bluetooth scan watchdog",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_scan_wdog,
	},
	{
		.test_id = "epacket_bt_gateway_connect",
		.test_descr = "Connect to peer device",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect,
	},
	{
		.test_id = "epacket_bt_gateway_connect_multi",
		.test_descr = "Connect to multiple peer devices",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_multi,
	},
	{
		.test_id = "epacket_bt_gateway_connect_then_scan",
		.test_descr = "Connect to peer device, then continue scanning",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_then_scan,
	},
	{
		.test_id = "epacket_bt_gateway_connect_rpc",
		.test_descr = "Bluetooth gateway RPCs",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_rpcs,
	},
	{
		.test_id = "epacket_bt_gateway_connect_recv",
		.test_descr = "Connect to peer device and recv payloads",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_recv,
	},
	{
		.test_id = "epacket_bt_gateway_connect_idle_tx_timeout",
		.test_descr = "Connect to peer device, test TX idle timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_idle_tx_timeout,
	},
	{
		.test_id = "epacket_bt_gateway_connect_idle_rx_timeout",
		.test_descr = "Connect to peer device, test RX idle timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_idle_rx_timeout,
	},
	{
		.test_id = "epacket_bt_gateway_connect_idle_rx_log_ignored",
		.test_descr = "Connect to peer device, ensure logging output ignored for "
			      "inactivity timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_idle_rx_log_ignored,
	},
	{
		.test_id = "epacket_bt_gateway_connect_absolute_timeout",
		.test_descr = "Connect to peer device, test absolute timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_absolute_timeout,
	},
	{
		.test_id = "epacket_bt_gateway_connect_absolute_timeout_update",
		.test_descr = "Connect to peer device, absolute timeout updated on each call",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_absolute_timeout_update,
	},
	{
		.test_id = "epacket_bt_gateway_connect_absolute_timeout_cancel",
		.test_descr = "Connect to peer device, test absolute timeout on disconnection",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect_absolute_timeout_cancel,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc",
		.test_descr = "Connect to peer device and run RPC",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_client,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward",
		.test_descr = "Connect to peer device and run RPC forwarded from serial",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn",
		.test_descr = "Run RPC forwarded from serial as INFUSE_EPACKET_FORWARD_AUTO_CONN",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_single",
		.test_descr = "Run INFUSE_EPACKET_FORWARD_AUTO_CONN with SINGLE_RPC",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_single,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_dc_notify",
		.test_descr = "Run INFUSE_EPACKET_FORWARD_AUTO_CONN with CONN_TERMINATED",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_dc_notify,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_fail",
		.test_descr = "Run INFUSE_EPACKET_FORWARD_AUTO_CONN that fails to connect",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_fail,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_auth_fail",
		.test_descr = "INFUSE_EPACKET_FORWARD_AUTO_CONN that fails authentication",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_auth_fail,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_rate_throughput",
		.test_descr = "Rate limiting intergration based on target throughput",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_rate_throughput,
	},
	{
		.test_id = "epacket_bt_gateway_remote_rpc_forward_auto_conn_rate_limit",
		.test_descr = "Rate limiting intergration based on pauses",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_remote_rpc_forward_auto_conn_rate_limit,
	},
	{
		.test_id = "epacket_bt_gateway_data_logger_read_throughput",
		.test_descr = "Data logger read integration",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_data_logger_read_throughput,
	},
	{
		.test_id = "epacket_bt_gateway_bt_file_copy",
		.test_descr = "Data logger read integration",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_bt_file_copy,
	},
	{
		.test_id = "epacket_bt_gateway_mcumgr_reboot",
		.test_descr = "Reboot remote device through MCUmgr",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_mcumgr_reboot,
	},
	{
		.test_id = "epacket_bt_gateway_mcumgr_none_reboot",
		.test_descr = "Try to reboot remote device that doesn't have MCUmgr",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_mcumgr_none_reboot,
	},
	BSTEST_END_MARKER};

struct bst_test_list *test_epacket_bt_gateway(struct bst_test_list *tests)
{
	return bst_add_tests(tests, epacket_gateway);
}

bst_test_install_t test_installers[] = {test_epacket_bt_gateway, NULL};

int main(void)
{
	bst_main();
	return 0;
}
