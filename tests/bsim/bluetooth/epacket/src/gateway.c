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
#include <infuse/rpc/client.h>

int epacket_bt_gatt_encrypt(struct net_buf *buf);

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
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct epacket_read_response security_info;
	struct bt_conn *conn, *conn2;
	bt_addr_le_t addr;
	int8_t rssi;
	int rc;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
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

static void main_gateway_connect_multi(void)
{
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct epacket_read_response security_info;
	struct bt_conn *conn1, *conn2;
	bt_addr_le_t addr[2];
	int rc;

	common_init();
	if (observe_peers(addr, 2) < 0) {
		FAIL("Failed to observe peers\n");
		return;
	}

	for (int i = 0; i < 3; i++) {
		/* Connect to first device */
		rc = epacket_bt_gatt_connect(&addr[0], &params, 3000, &conn1, &security_info, false,
					     false, false);
		if (rc != 0) {
			FAIL("Failed to connect to first peer\n");
			return;
		}

		/* Connect to the second device */
		rc = epacket_bt_gatt_connect(&addr[1], &params, 3000, &conn2, &security_info, false,
					     false, false);
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
		rc = bt_conn_disconnect_sync(conn2);
		if (rc != 0) {
			FAIL("Failed to disconnect from second peer\n");
			return;
		}
		bt_conn_unref(conn2);
	}

	PASS("Received packets from advertiser\n");
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
	if (observe_peers(&addr, 1) < 0) {
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

static K_FIFO_DEFINE(central_fifo);

void central_handler(struct net_buf *buf)
{
	k_fifo_put(&central_fifo, buf);
}

static void main_gateway_connect_recv(void)
{
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_read_response security_info;
	struct epacket_rx_metadata *meta;
	struct bt_conn *conn;
	struct net_buf *buf;
	bt_addr_le_t addr;
	bool data_sub;
	int rc;

	common_init();
	epacket_set_receive_handler(epacket_central, central_handler);

	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}

	for (int i = 0; i < 4; i++) {
		data_sub = i % 2;

		/* Connect to peer device */
		rc = epacket_bt_gatt_connect(&addr, &params, 3000, &conn, &security_info, false,
					     data_sub, false);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		if (data_sub) {
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
	}

	PASS("Received TDF data from connected peer\n");
}

static void main_gateway_remote_rpc_client(void)
{
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct epacket_read_response security_info;
	union epacket_interface_address address, wrong;
	struct bt_conn *conn;
	struct net_buf *buf;
	bt_addr_le_t addr;
	int rc;

	struct rpc_application_info_request req;
	struct rpc_application_info_response *rsp;
	struct rpc_client_ctx ctx;

	common_init();

	if (observe_peers(&addr, 1) < 0) {
		FAIL("Failed to observe peer\n");
		return;
	}
	address.bluetooth = addr;
	rpc_client_init(&ctx, epacket_central, address);

	for (int i = 0; i < 4; i++) {
		/* Connect to peer device */
		rc = epacket_bt_gatt_connect(&addr, &params, 3000, &conn, &security_info, true,
					     false, false);
		if (rc != 0) {
			FAIL("Failed to connect to peer\n");
			return;
		}

		/* Send to incorrect device */
		wrong.bluetooth = addr;
		wrong.bluetooth.a.val[0] += 1;
		for (int i = 0; i < 5; i++) {
			buf = epacket_alloc_tx_for_interface(epacket_central, K_MSEC(1));
			if (buf == NULL) {
				FAIL("Failed to allocate buffer");
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
			FAIL("Failed to query version (%d)", rc);
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

static void main_gateway_remote_rpc_forward(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	const struct device *epacket_central = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_central));
	struct k_fifo *response_queue = epacket_dummmy_transmit_fifo_get();
	struct rpc_bt_connect_infuse_response *connect_rsp;
	union epacket_interface_address address;
	struct net_buf *buf;
	bt_addr_le_t addr;
	int rc;

	common_init();
	if (observe_peers(&addr, 1) < 0) {
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
		.subscribe = RPC_ENUM_INFUSE_BT_CHARACTERISTIC_COMMAND,
		.inactivity_timeout_ms = 0,
	};
	struct rpc_bt_disconnect_request disconnect = {
		.peer = connect.peer,
	};

	struct rpc_application_info_request info_request;
	struct rpc_application_info_response *info_rsp;

	epacket_set_receive_handler(epacket_dummy, dummy_gateway_handler);
	epacket_set_receive_handler(epacket_central, dummy_gateway_handler);

	for (int i = 0; i < 3; i++) {
		/* Connect to the remote device */
		send_rpc(1, RPC_ID_BT_CONNECT_INFUSE, &connect, sizeof(connect));
		buf = expect_response(1, RPC_ID_BT_CONNECT_INFUSE, 0);
		if (buf == NULL) {
			FAIL("Failed to connect via RPC");
			return;
		}
		connect_rsp = (void *)buf->data;
		net_buf_unref(buf);

		/* Create and encrypt the GATT RPC */
		address.bluetooth = addr;
		info_request.header.command_id = RPC_ID_APPLICATION_INFO;
		info_request.header.request_id = 0x12345678;
		buf = epacket_alloc_tx_for_interface(epacket_central, K_FOREVER);
		epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0, INFUSE_RPC_CMD,
					EPACKET_ADDR_ALL);
		net_buf_add_mem(buf, &info_request, sizeof(info_request));
		rc = epacket_bt_gatt_encrypt(buf);
		if (rc < 0) {
			FAIL("Failed to encrypt GATT RPC");
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
			FAIL("Failed to receive response");
			return;
		}

		struct epacket_dummy_frame *frame;
		struct epacket_received_common_header *common_header;
		struct epacket_received_decrypted_header *decr_header;

		frame = net_buf_pull_mem(buf, sizeof(struct epacket_dummy_frame));
		if (frame->type != INFUSE_RECEIVED_EPACKET) {
			FAIL("Unexpected packet type");
			return;
		}
		common_header =
			net_buf_pull_mem(buf, sizeof(struct epacket_received_common_header));
		if (common_header->interface != EPACKET_INTERFACE_BT_CENTRAL) {
			FAIL("Unexpected interface");
			return;
		}
		net_buf_pull_mem(buf, sizeof(struct epacket_interface_address_bt_le));
		decr_header =
			net_buf_pull_mem(buf, sizeof(struct epacket_received_decrypted_header));
		if (decr_header->type != INFUSE_RPC_RSP) {
			FAIL("Unexpected packet type");
			return;
		}

		info_rsp = (void *)buf->data;
		if (info_rsp->header.request_id != info_request.header.request_id) {
			FAIL("Unexpected request ID");
			return;
		}
		if (info_rsp->header.command_id != info_request.header.command_id) {
			FAIL("Unexpected command ID");
			return;
		}
		if (info_rsp->header.return_code != 0) {
			FAIL("Unexpected return code");
			return;
		}
		/* This only works because both devices have the same timebase due to the
		 * simulation
		 */
		if (info_rsp->uptime != k_uptime_seconds()) {
			FAIL("Unexpected uptime");
			return;
		}
		LOG_HEXDUMP_INF(buf->data, buf->len, "RPC Response");
		net_buf_unref(buf);

		/* Disconnect from the remote device */
		send_rpc(2, RPC_ID_BT_DISCONNECT, &disconnect, sizeof(disconnect));
		buf = expect_response(2, RPC_ID_BT_DISCONNECT, 0);
		if (buf == NULL) {
			FAIL("Unexpected disconnection result");
			return;
		}
		net_buf_unref(buf);
	}

	PASS("RPC forwarder passed\n");
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
