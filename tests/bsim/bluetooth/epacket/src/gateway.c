/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

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
#include <infuse/rpc/types.h>

extern enum bst_result_t bst_result;
static K_SEM_DEFINE(epacket_adv_received, 0, 1);
static bt_addr_le_t adv_device;
static atomic_t received_packets;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void common_init(void)
{
	k_sem_reset(&epacket_adv_received);
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

static void main_gateway_connect(void)
{
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct epacket_read_response security_info;
	struct bt_conn *conn, *conn2;
	bt_addr_le_t addr;
	int8_t rssi;
	int rc;

	common_init();
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 5; i++) {
		/* Initiate connection */
		rc = epacket_bt_gatt_connect(&addr, &params, 3000, &conn, &security_info, i % 2,
					     i % 2, i % 2);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		/* Same connection again should pass with RC == 1 */
		rc = epacket_bt_gatt_connect(&addr, &params, 3000, &conn2, &security_info, i % 2,
					     i % 2, i % 2);
		if (rc != 1) {
			FAIL("Failed to detect existing connection");
			return;
		}
		bt_conn_unref(conn2);

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
	}

	k_sleep(K_SECONDS(1));

	PASS("Gateway connect passed\n");
}

static void main_gateway_connect_then_scan(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct epacket_read_response security_info;
	struct bt_conn *conn;
	bt_addr_le_t addr;
	int rc;

	common_init();
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer");
		return;
	}

	/* Initiate connection */
	rc = epacket_bt_gatt_connect(&addr, &params, 3000, &conn, &security_info, false, false,
				     false);
	if (rc < 0) {
		FAIL("Failed to connect to peer");
		return;
	}

	/* Start scanning again */
	if (epacket_receive(epacket_bt_adv, K_FOREVER) < 0) {
		FAIL("Failed to resume scanning");
	}
	k_sleep(K_SECONDS(6));
	if (epacket_receive(epacket_bt_adv, K_NO_WAIT) < 0) {
		FAIL("Failed to terminate scanning");
	}

	/* Terminate the connection */
	rc = bt_conn_disconnect_sync(conn);
	if (rc < 0) {
		FAIL("Failed to disconnect from peer");
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
	struct net_buf *rsp;

	/* Response was sent */
	rsp = net_buf_get(response_queue, K_SECONDS(10));
	if (rsp == NULL) {
		LOG_ERR("No response");
		return NULL;
	}
	net_buf_pull_mem(rsp, sizeof(struct epacket_dummy_frame));
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
	if (observe_peer(&addr) < 0) {
		FAIL("Failed to observe peer");
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
		FAIL("Failed to connect via RPC");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(2, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(2, RPC_ID_BT_DISCONNECT, 0);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result");
		return;
	}
	net_buf_unref(buf);

	/* Connect timeout, disconnect should error */
	connect.conn_timeout_ms = 10;
	send_rpc(3, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
	buf = expect_response(3, RPC_ID_BT_CONNECT_INFUSE, -BT_HCI_ERR_UNKNOWN_CONN_ID);
	if (buf == NULL) {
		FAIL("Unexpected connection result");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(4, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(4, RPC_ID_BT_DISCONNECT, -EINVAL);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result");
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
		FAIL("Failed to connect via RPC");
		return;
	}
	connect_rsp = (void *)buf->data;
	net_buf_unref(buf);

	send_rpc(6, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
	buf = expect_response(6, RPC_ID_BT_DISCONNECT, 0);
	if (buf == NULL) {
		FAIL("Unexpected disconnection result");
		return;
	}
	net_buf_unref(buf);

	PASS("RPC connecter passed\n");
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
		.test_id = "epacket_bt_gateway_connect",
		.test_descr = "Connect to peer device",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_gateway_connect,
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
