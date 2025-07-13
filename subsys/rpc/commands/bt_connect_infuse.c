/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include "common_bt.h"

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_bt_connect_infuse(struct net_buf *request)
{
	struct rpc_bt_connect_infuse_request *req = (void *)request->data;
	struct rpc_bt_connect_infuse_response rsp = {
		.peer = req->peer,
	};
	const bt_addr_le_t peer = bt_addr_infuse_to_zephyr(&req->peer);
	const struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400);
	struct epacket_read_response security_info;
	k_timeout_t inactivty =
		req->inactivity_timeout_ms == 0 ? K_FOREVER : K_MSEC(req->inactivity_timeout_ms);
	struct bt_conn *conn;
	int rc;

	if (req->inactivity_timeout_ms != 0) {
		LOG_WRN("Inactivity timeout not yet supported");
	}

	/* Run the connection process */
	rc = epacket_bt_gatt_connect(&peer, &params, req->conn_timeout_ms, &conn, &security_info,
				     req->subscribe & RPC_ENUM_INFUSE_BT_CHARACTERISTIC_COMMAND,
				     req->subscribe & RPC_ENUM_INFUSE_BT_CHARACTERISTIC_DATA,
				     req->subscribe & RPC_ENUM_INFUSE_BT_CHARACTERISTIC_LOGGING,
				     inactivty);
	if (rc >= 0) {
		/* Copy results */
		memcpy(rsp.cloud_public_key, security_info.cloud_public_key,
		       sizeof(rsp.cloud_public_key));
		memcpy(rsp.device_public_key, security_info.device_public_key,
		       sizeof(rsp.device_public_key));
		rsp.network_id = security_info.network_id;
		bt_conn_unref(conn);
	}

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
