/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/conn.h>

#include <infuse/bluetooth/gatt.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

#include "common_coap.h"
#include "common_bt.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_bt_file_copy_coap(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *rsp_interface = req_meta->interface;
	struct rpc_bt_file_copy_coap_request *req = (void *)request->data;
	struct rpc_bt_file_copy_coap_response rsp = {0};
	int downloaded;
	int rc;

	/* Setup arguments for sub-commands */
	struct rpc_coap_download_request coap_req = {
		.server_port = req->server_port,
		.block_timeout_ms = req->block_timeout_ms,
		.action = RPC_ENUM_FILE_ACTION_FILE_FOR_COPY,
		.resource_len = req->resource_len,
		.resource_crc = req->resource_crc,
	};
	struct rpc_bt_file_copy_basic_request copy_req = {
		.peer = req->peer,
		.action = req->action,
		.file_idx = 0,
		.ack_period = req->ack_period,
		.pipelining = req->pipelining,
	};
	struct rpc_coap_download_response coap_rsp = {0};
	struct rpc_bt_file_copy_basic_response copy_rsp = {0};
	struct epacket_bt_gatt_connect_params connect_params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.peer = bt_addr_infuse_to_zephyr(&req->peer),
		.inactivity_timeout = K_FOREVER,
		.absolute_timeout = K_FOREVER,
		.conn_timeout_ms = req->conn_timeout_ms,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = false,
		.subscribe_logging = false,
	};

	memcpy(coap_req.server_address, req->server_address, sizeof(req->server_address));

	/* COAP file download */
	LOG_INF("Download '%s' from %s:%d", req->resource, coap_req.server_address,
		coap_req.server_port);
	rc = rpc_command_coap_download_run(&coap_req, req->resource, &coap_rsp, &downloaded);
	rsp.resource_len = coap_rsp.resource_len;
	rsp.resource_crc = coap_rsp.resource_crc;

	if (downloaded == 0) {
		/* Flash already matched the request, reuse the request */
		copy_req.file_len = coap_req.resource_len;
		copy_req.file_crc = coap_req.resource_crc;
	} else {
		/* Use the file parameters downloaded from COAP */
		copy_req.file_len = rsp.resource_len;
		copy_req.file_crc = rsp.resource_crc;
	}

	/* Free the request packet (we no longer need req->resource) */
	rpc_command_runner_request_unref(request);
	if (rc < 0) {
		LOG_INF("Download failed");
		goto end;
	}

	/* Create the Bluetooth connection */
	struct epacket_read_response security_info;
	struct bt_conn *conn;

	LOG_INF("Initiating connection");
	rc = epacket_bt_gatt_connect(&conn, &connect_params, &security_info);
	if (rc != 0) {
		LOG_INF("Connection failed (%d)", rc);
		rc = -ENOTCONN;
		goto end;
	}

	/* Run the file copy */
	LOG_INF("Copying %d byte file (CRC %08X)", copy_req.file_len, copy_req.file_crc);
	rc = rpc_command_bt_file_copy_basic_run(&copy_req, &copy_rsp);

	/* Terminate the connection */
	(void)bt_conn_disconnect_sync(conn);
	bt_conn_unref(conn);
end:
	/* Return the response */
	return rpc_response_simple_if(rsp_interface, rc, &rsp, sizeof(rsp));
}
