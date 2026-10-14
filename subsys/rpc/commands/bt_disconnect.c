/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <infuse/bluetooth/gatt.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include "common_bt.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct disconnect_all_context {
	bool found;
	bool failed;
};

static void disconnect_central(struct bt_conn *conn, void *user_data)
{
	struct disconnect_all_context *context = user_data;
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) < 0 || info.state != BT_CONN_STATE_CONNECTED ||
	    info.role != BT_CONN_ROLE_CENTRAL) {
		return;
	}

	context->found = true;
	if (bt_conn_disconnect_sync(conn) < 0) {
		context->failed = true;
	}
}

struct net_buf *rpc_command_bt_disconnect(struct net_buf *request)
{
	struct rpc_bt_disconnect_request *req = (void *)request->data;
	struct rpc_bt_disconnect_response rsp = {0};
	const bt_addr_le_t peer = bt_addr_infuse_to_zephyr(&req->peer);
	struct bt_conn *conn;
	int rc = 0;

	if (bt_addr_le_eq(&peer, BT_ADDR_LE_ANY)) {
		struct disconnect_all_context context = {0};

		bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_central, &context);
		if (!context.found) {
			rc = INFUSE_RPC_ERROR_BT_NOT_CONNECTED;
		} else if (context.failed) {
			rc = INFUSE_RPC_ERROR_BT_DISCONNECT_FAILED;
		}
		goto end;
	}

	/* Find the connection from the address */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &peer);
	if (conn == NULL) {
		/* No connection exists */
		rc = INFUSE_RPC_ERROR_BT_NOT_CONNECTED;
		goto end;
	}

	/* Disconnect from the remote */
	rc = bt_conn_disconnect_sync(conn);
	if (rc < 0) {
		rc = INFUSE_RPC_ERROR_BT_DISCONNECT_FAILED;
	}

	/* Unreference the connection object claimed in bt_conn_lookup_addr_le */
	bt_conn_unref(conn);
end:
	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
