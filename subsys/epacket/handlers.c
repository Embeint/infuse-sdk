/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/server.h>

LOG_MODULE_DECLARE(epacket);

void epacket_default_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_DBG("Received on %s: Auth=%d Type=%d Seq=%d Len=%d", meta->interface->name, meta->auth,
		meta->type, meta->sequence, buf->len);

	if (meta->auth == EPACKET_AUTH_FAILURE) {
		goto done;
	}

	if (meta->type == INFUSE_ECHO_REQ) {
		/* Respond to valid echo requests */
		struct net_buf *echo = epacket_alloc_tx_for_interface(meta->interface, K_NO_WAIT);

		if (echo == NULL) {
			LOG_WRN("Failed to allocate echo response");
		} else {
			epacket_set_tx_metadata(echo, meta->auth, 0, INFUSE_ECHO_RSP,
						EPACKET_ADDR_ALL);
			net_buf_add_mem(echo, buf->data, buf->len);
			epacket_queue(meta->interface, echo);
		}
	}
#ifdef CONFIG_INFUSE_RPC
	if (meta->type == INFUSE_RPC_CMD) {
		rpc_server_queue_command(buf);
		return;
	}
	if (meta->type == INFUSE_RPC_DATA) {
		rpc_server_queue_data(buf);
		return;
	}
#endif /* CONFIG_INFUSE_RPC */

done:
	net_buf_unref(buf);
}

void epacket_gateway_receive_handler(const struct device *backhaul, struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	/* Forward Bluetooth advertising packets */
	if (meta->interface_id == EPACKET_INTERFACE_BT_ADV) {
		LOG_DBG("Received on %s: Auth=%d Type=%d Seq=%d Len=%d", meta->interface->name,
			meta->auth, meta->type, meta->sequence, buf->len);

		struct net_buf *forward = epacket_alloc_tx_for_interface(backhaul, K_FOREVER);

		if (epacket_received_packet_append(forward, buf) == 0) {
			/* Add metadata */
			epacket_set_tx_metadata(forward, EPACKET_AUTH_DEVICE, 0x00,
						INFUSE_RECEIVED_EPACKET, EPACKET_ADDR_ALL);
			/* Queue for transmission on backhaul */
			epacket_queue(backhaul, forward);
		} else {
			LOG_WRN("Could not forward packet");
			net_buf_unref(forward);
			net_buf_unref(buf);
		}
		return;
	}

	/* Run default handler */
	epacket_default_receive_handler(buf);
}

int epacket_received_packet_append(struct net_buf *storage_buf, struct net_buf *received_buf)
{
	struct epacket_rx_metadata *rx_meta = net_buf_user_data(received_buf);
	struct epacket_received_common_header common;
	struct epacket_received_decrypted_header decrypted;
	struct epacket_interface_address_bt_le addr_encoded;
	uint8_t addr_len = 0;

	/* Determine total length */
	common.len_encrypted = sizeof(common) + received_buf->len;
	if (rx_meta->auth != EPACKET_AUTH_FAILURE) {
		common.len_encrypted += sizeof(decrypted);
	}
	switch (rx_meta->interface_id) {
	case EPACKET_INTERFACE_BT_ADV:
	case EPACKET_INTERFACE_BT_CENTRAL:
		addr_encoded.type = rx_meta->interface_address.bluetooth.type;
		memcpy(addr_encoded.addr, rx_meta->interface_address.bluetooth.a.val, 6);
		addr_len = sizeof(addr_encoded);
		break;
	default:
		break;
	}
	common.len_encrypted += addr_len;

	/* Validate size against storage buffer capacity */
	if (net_buf_tailroom(storage_buf) < common.len_encrypted) {
		return -ENOMEM;
	}

	/* Common header + interface address */
	common.len_encrypted |= rx_meta->auth == EPACKET_AUTH_FAILURE ? 0x8000 : 0x00;
	common.interface = rx_meta->interface_id;
	common.rssi = -MIN(0, rx_meta->rssi);
	net_buf_add_mem(storage_buf, &common, sizeof(common));
	net_buf_add_mem(storage_buf, &addr_encoded, addr_len);

	if (rx_meta->auth != EPACKET_AUTH_FAILURE) {
		/* Decrypted data header */
		decrypted.type = rx_meta->type;
		decrypted.device_id = rx_meta->packet_device_id;
		decrypted.gps_time = rx_meta->packet_gps_time;
		decrypted.flags = rx_meta->flags;
		decrypted.sequence = rx_meta->sequence;
		sys_put_le24(rx_meta->key_identifier, decrypted.key_id);

		net_buf_add_mem(storage_buf, &decrypted, sizeof(decrypted));
	}
	/* Payload */
	net_buf_add_mem(storage_buf, received_buf->data, received_buf->len);

	net_buf_unref(received_buf);
	return 0;
}
