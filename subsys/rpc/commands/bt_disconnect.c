/**
 * @file
 * @copyright 2024 Embeint Inc
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

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_bt_disconnect(struct net_buf *request)
{
	struct rpc_bt_disconnect_request *req = (void *)request->data;
	struct rpc_bt_disconnect_response rsp = {0};
	const bt_addr_le_t peer = bt_addr_infuse_to_zephyr(&req->peer);
	struct bt_conn *conn;
	int rc = 0;

	/* Find the connection from the address */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &peer);
	if (conn == NULL) {
		/* No connection exists */
		rc = -EINVAL;
		goto end;
	}

	/* Disconnect from the remote */
	rc = bt_conn_disconnect_sync(conn);

	/* Unreference the connection object claimed in bt_conn_lookup_addr_le */
	bt_conn_unref(conn);
end:
	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
