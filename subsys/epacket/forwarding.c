/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/conn.h>

#include <infuse/states.h>
#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/rpc/server.h>

LOG_MODULE_DECLARE(epacket, CONFIG_EPACKET_LOG_LEVEL);

struct conn_state {
	uint8_t flags;
};

static K_FIFO_DEFINE(packet_queue);
static const struct device *epacket_backhaul;
static struct conn_state forwarding_state[CONFIG_BT_MAX_CONN];

BUILD_ASSERT(CONFIG_BT_MAX_CONN <= 32, "rpc_disconnect_mask");

static void epacket_forward_direct(struct net_buf *buf)
{
	struct epacket_forward_header *hdr =
		net_buf_pull_mem(buf, sizeof(struct epacket_forward_header));
	const struct device *forward_interface;
	struct epacket_interface_address_bt_le *dest_encoded;
	union epacket_interface_address dest;
	uint16_t forward_payload, forward_max_size;
	struct net_buf *tx;

	/* Only Bluetooth addresses are currently handled */
	dest_encoded = net_buf_pull_mem(buf, sizeof(*dest_encoded));
	dest.bluetooth.type = dest_encoded->type;
	memcpy(dest.bluetooth.a.val, dest_encoded->addr, 6);

	switch (hdr->interface) {
	case EPACKET_INTERFACE_BT_CENTRAL:
		forward_interface = DEVICE_DT_GET_ONE(embeint_epacket_bt_central);
		break;
	default:
		LOG_WRN("Unknown interface ID: %d", hdr->interface);
		goto cleanup;
	}

	/* Validate that forwarding interface can support required packet size */
	forward_max_size = epacket_interface_max_packet_size(forward_interface);
	forward_payload = hdr->length - sizeof(*hdr) - 7;
	if (forward_max_size < forward_payload) {
		LOG_WRN("Insufficient packet size (%d < %d)", forward_max_size, forward_payload);
		goto cleanup;
	}

	/* Allocate buffer for forwarded message */
	tx = epacket_alloc_tx(K_MSEC(10));
	if (tx == NULL) {
		LOG_WRN("Unable to allocate buffer");
		goto cleanup;
	}

	epacket_set_tx_metadata(tx, EPACKET_AUTH_REMOTE_ENCRYPTED, 0, 0, dest);
	net_buf_add_mem(tx, buf->data, forward_payload);

	epacket_queue(forward_interface, tx);

cleanup:
	net_buf_unref(buf);
}

void epacket_packet_forward(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	if ((meta->auth != EPACKET_AUTH_DEVICE) && (meta->auth != EPACKET_AUTH_NETWORK)) {
		LOG_WRN("Cannot handle forwarding packet with failed auth (%d)", meta->auth);
		net_buf_unref(buf);
		return;
	}

	epacket_backhaul = meta->interface;
	if (meta->type == INFUSE_EPACKET_FORWARD) {
		epacket_forward_direct(buf);
	} else if (meta->type == INFUSE_EPACKET_FORWARD_AUTO_CONN) {
		/* Push the buffer into the FIFO for the thread to handle */
		k_fifo_put(&packet_queue, buf);
	}
}

static int ensure_bt_connection(union epacket_interface_address *address, uint8_t flags,
				uint32_t conn_timeout_ms, k_timeout_t idle_timeout,
				k_timeout_t absolute_timeout)
{
	struct epacket_bt_gatt_connect_params params = {
		.conn_params = BT_LE_CONN_PARAM_INIT(0x10, 0x15, 0, 400),
		.peer = address->bluetooth,
		.inactivity_timeout = idle_timeout,
		.absolute_timeout = absolute_timeout,
		.conn_timeout_ms = conn_timeout_ms,
		.preferred_phy = BT_GAP_LE_PHY_NONE,
		.subscribe_commands = true,
		.subscribe_data = flags & EPACKET_FORWARD_AUTO_CONN_SUB_DATA,
		.subscribe_logging = false,
	};
	struct epacket_read_response security_info;
	struct bt_conn *conn = NULL;
	struct conn_state *state;
	bool throughput_limit = false;
	int conn_idx;
	int rc;

	/* Create the connection */
	rc = epacket_bt_gatt_connect(&conn, &params, &security_info);
	if (rc != 0) {
		/* Connection failed */
		return rc;
	}

#ifdef CONFIG_KV_STORE_KEY_BLUETOOTH_THROUGHPUT_LIMIT
	struct kv_bluetooth_throughput_limit limit;

	if (KV_STORE_READ(KV_KEY_BLUETOOTH_THROUGHPUT_LIMIT, &limit) == sizeof(limit)) {
		LOG_INF("Requesting throughput limit of %d kbps", limit.limit_kbps);
		/* Throughput limit has been set */
		rc = epacket_bt_gatt_rate_throughput_request(conn, limit.limit_kbps);
		if (rc != 0) {
			LOG_WRN("Failed to request throughput limit (%d)", rc);
		}
		/* Uplink is known to be limited */
		throughput_limit = true;
	}
#endif /* CONFIG_KV_STORE_KEY_BLUETOOTH_THROUGHPUT_LIMIT */

	/* Store whether we should disconnect on receiving a RPC response */
	conn_idx = bt_conn_index(conn);
	state = &forwarding_state[conn_idx];
	state->flags = flags & (EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC |
				EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION);
	if (throughput_limit && (flags & EPACKET_FORWARD_AUTO_CONN_PRIORITISE_UPLINK)) {
		state->flags |= EPACKET_FORWARD_AUTO_CONN_PRIORITISE_UPLINK;
		infuse_state_set_timeout(INFUSE_STATE_HIGH_PRIORITY_UPLINK, 2);
	}

	/* Unreference the connection for the idle timeout */
	bt_conn_unref(conn);
	return 0;
}

static void send_conn_terminated(const struct device *backhaul, int16_t reason,
				 const bt_addr_le_t *dst)
{
	struct epacket_interface_address_bt_le if_address;
	struct epacket_conn_terminated terminated = {
		.interface = EPACKET_INTERFACE_BT_CENTRAL,
		.reason = reason,
	};
	struct net_buf *tx;

	/* Allocate the packet */
	tx = epacket_alloc_tx_for_interface(backhaul, K_NO_WAIT);
	if (tx == NULL) {
		return;
	}

	if_address.type = dst->type;
	memcpy(if_address.addr, dst->a.val, sizeof(if_address.addr));

	/* Send the INFUSE_EPACKET_CONN_TERMINATED packet */
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0, INFUSE_EPACKET_CONN_TERMINATED,
				EPACKET_ADDR_ALL);
	net_buf_add_mem(tx, &terminated, sizeof(terminated));
	net_buf_add_mem(tx, &if_address, sizeof(if_address));
	epacket_queue(backhaul, tx);
	LOG_DBG("Queued CONN_TERMINATED");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int conn_id = bt_conn_index(conn);
	struct conn_state *state = &forwarding_state[conn_id];

	if (state->flags & EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION) {
		send_conn_terminated(epacket_backhaul, reason, bt_conn_get_dst(conn));
	}

	/* Clear the masks */
	state->flags = 0;
}

bool bt_central_packet_received(struct net_buf *buf, bool decrypted, void *user_ctx)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct bt_conn *conn = NULL;
	struct conn_state *state;
	int conn_id, rc;

	/* Find the associated connection object */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &meta->interface_address.bluetooth);
	if (conn == NULL) {
		/* Connection disconnected between RX and now */
		return true;
	}
	conn_id = bt_conn_index(conn);
	state = &forwarding_state[conn_id];

	if (state->flags & EPACKET_FORWARD_AUTO_CONN_PRIORITISE_UPLINK) {
		/* Data received on the link, continue prioritising it */
		infuse_state_set_timeout(INFUSE_STATE_HIGH_PRIORITY_UPLINK, 2);
	}

	/* Is this RPC_RSP received on a connection with EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC */
	if ((meta->type == INFUSE_RPC_RSP) &&
	    (state->flags & EPACKET_FORWARD_AUTO_CONN_SINGLE_RPC)) {
		LOG_INF("Initiating disconnect due to RPC_RSP");
		rc = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (rc != 0) {
			LOG_ERR("Failed to initiate disconnection (%d)", rc);
		}
	}
	bt_conn_unref(conn);
	return true;
}

static void forward_auto_conn_processor(void *a, void *b, void *c)
{
	const struct device *bt_central = DEVICE_DT_GET_ONE(embeint_epacket_bt_central);
	static struct epacket_interface_cb bt_central_cb;
	static struct bt_conn_cb conn_cb;
	struct epacket_forward_auto_conn_header *hdr;
	const struct device *forward_interface;
	struct epacket_interface_address_bt_le *dest_encoded;
	union epacket_interface_address dest;
	uint16_t forward_payload, forward_max_size;
	struct epacket_rx_metadata *meta;
	struct net_buf *buf;
	struct net_buf *tx;
	int rc;

	k_thread_name_set(NULL, "auto_conn_forward");

	/* Callback registration */
	conn_cb.disconnected = disconnected;
	bt_conn_cb_register(&conn_cb);
	bt_central_cb.packet_received = bt_central_packet_received;
	epacket_register_callback(bt_central, &bt_central_cb);

	for (;;) {
		buf = k_fifo_get(&packet_queue, K_FOREVER);
		meta = net_buf_user_data(buf);
		hdr = net_buf_pull_mem(buf, sizeof(struct epacket_forward_auto_conn_header));

		switch (hdr->interface) {
		case EPACKET_INTERFACE_BT_CENTRAL:
			forward_interface = bt_central;
			break;
		default:
			LOG_WRN("Unknown interface ID: %d", hdr->interface);
			goto cleanup;
		}

		/* Only Bluetooth addresses are currently handled */
		dest_encoded = net_buf_pull_mem(buf, sizeof(*dest_encoded));
		dest.bluetooth.type = dest_encoded->type;
		memcpy(dest.bluetooth.a.val, dest_encoded->addr, 6);

		/* Ensure we have a valid Bluetooth connection before sending */
		rc = ensure_bt_connection(
			&dest, hdr->flags, (uint32_t)hdr->conn_timeout * MSEC_PER_SEC,
			K_SECONDS(hdr->conn_idle_timeout), K_SECONDS(hdr->conn_absolute_timeout));
		if (rc != 0) {
			if (hdr->flags & EPACKET_FORWARD_AUTO_CONN_DC_NOTIFICATION) {
				send_conn_terminated(meta->interface, rc, &dest.bluetooth);
			}
			goto cleanup;
		}

		/* Validate that forwarding interface can support required packet size */
		forward_max_size = epacket_interface_max_packet_size(forward_interface);
		forward_payload = hdr->length - sizeof(*hdr) - 7;
		if (forward_max_size < forward_payload) {
			LOG_WRN("Insufficient packet size (%d < %d)", forward_max_size,
				forward_payload);
			goto cleanup;
		}

		/* Allocate buffer for forwarded message */
		tx = epacket_alloc_tx(K_MSEC(10));
		if (tx == NULL) {
			LOG_WRN("Unable to allocate buffer");
			goto cleanup;
		}

		/* Copy across to the TX message, push to transmit queue */
		epacket_set_tx_metadata(tx, EPACKET_AUTH_REMOTE_ENCRYPTED, 0, 0, dest);
		net_buf_add_mem(tx, buf->data, forward_payload);
		epacket_queue(forward_interface, tx);
cleanup:
		net_buf_unref(buf);
	}
}

K_THREAD_DEFINE(auto_conn_thread, 2048, forward_auto_conn_processor, NULL, NULL, NULL, 5, 0, 0);
