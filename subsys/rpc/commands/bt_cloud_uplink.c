/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt_peripheral.h>
#include <infuse/lib/memfault.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

#ifdef CONFIG_INFUSE_MEMFAULT
static int cloud_uplink_send(const struct device *dev, struct net_buf *buf, void *user_ctx)
{
	struct bt_conn *conn = user_ctx;

	return epacket_bt_peripheral_cloud_uplink_send(dev, conn, buf);
}
#endif /* CONFIG_INFUSE_MEMFAULT */

struct net_buf *rpc_command_bt_cloud_uplink(struct net_buf *request)
{
	struct rpc_bt_cloud_uplink_response rsp = {0};
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	struct bt_conn *conn;
	int rc;

	if (req_meta->interface_id != EPACKET_INTERFACE_BT_PERIPHERAL) {
		rc = INFUSE_RPC_ERROR_UNSUPPORTED_REQUEST;
		goto end;
	}

	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &req_meta->interface_address.bluetooth);
	if (conn == NULL) {
		rc = INFUSE_RPC_ERROR_BT_NOT_CONNECTED;
		goto end;
	}

	if (!epacket_bt_peripheral_cloud_uplink_subscribed(req_meta->interface, conn)) {
		rc = INFUSE_RPC_ERROR_BT_CHARACTERISTIC_NOT_SUBSCRIBED;
		goto unref_conn;
	}

#ifdef CONFIG_INFUSE_MEMFAULT
	rc = infuse_memfault_dump_chunks_epacket_cb(req_meta->interface, cloud_uplink_send, conn);
	switch (rc) {
	case 0:
		break;
	case -ENODATA:
		rc = INFUSE_RPC_ERROR_NO_DATA;
		break;
	case -EAGAIN:
		rc = INFUSE_RPC_ERROR_BT_COMMAND_QUEUE_FAILED;
		break;
	case -ENOTCONN:
		rc = INFUSE_RPC_ERROR_BT_NOT_CONNECTED;
		break;
	default:
		rc = INFUSE_RPC_ERROR_BT_GATT_WRITE_FAILED;
		break;
	}
#endif /* CONFIG_INFUSE_MEMFAULT */

unref_conn:
	bt_conn_unref(conn);
end:
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
