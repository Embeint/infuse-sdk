/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/server.h>
#include <infuse/states.h>

#ifdef CONFIG_EPACKET_INTERFACE_BT_CENTRAL
#include <infuse/epacket/interface/epacket_bt_central.h>
#endif /* CONFIG_EPACKET_INTERFACE_BT_CENTRAL */

#include "forwarding.h"

#ifdef CONFIG_EPACKET_RECEIVE_GROUPING
const struct device *pending_backhaul;
static struct k_work_delayable pending_flush_worker;
static struct net_buf *pending_buffer;
static struct k_spinlock pending_lock;
#endif /* CONFIG_EPACKET_RECEIVE_GROUPING */

LOG_MODULE_DECLARE(epacket, CONFIG_EPACKET_LOG_LEVEL);

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

#ifdef CONFIG_EPACKET_RECEIVE_GROUPING

static void receive_do_flush(struct k_work *work)
{
	K_SPINLOCK(&pending_lock) {
		if (pending_buffer) {
			LOG_DBG("Flushing buffer %p to %s", pending_buffer, pending_backhaul->name);
			/* Queue for transmission on backhaul */
			epacket_queue(pending_backhaul, pending_buffer);
			pending_buffer = NULL;
		}
	}
}

static void receive_forward(const struct device *backhaul, struct net_buf *buf)
{
	const uint32_t max_hold = CONFIG_EPACKET_RECEIVE_GROUPING_MAX_HOLD_MS;
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	uint8_t rx_type = meta->type;
	static bool is_init;
	struct net_buf *temp;
	bool appended = false;

	if (!is_init) {
		k_work_init_delayable(&pending_flush_worker, receive_do_flush);
		is_init = true;
	}

#ifdef CONFIG_EPACKET_INTERFACE_BT_CENTRAL
	if (epacket_num_buffers_free_tx() <= CONFIG_EPACKET_RATE_LIMIT_BUFFER_THRESHOLD) {
		/* Running out of buffers, request a pause */
		LOG_DBG("Requesting rate limit");
		epacket_bt_gatt_rate_limit_request(CONFIG_EPACKET_RATE_LIMIT_REQ_DURATION_MS);
	}
#endif /* CONFIG_EPACKET_INTERFACE_BT_CENTRAL */

	K_SPINLOCK(&pending_lock) {
		if (pending_buffer) {
			/* We already have a buffer holding data */
			if (epacket_received_packet_append(pending_buffer, buf) == 0) {
				/* Append succeeded, buf has been freed */
				appended = true;
				buf = NULL;
				meta = NULL;
				/* RPC_RSP packets should trigger an immediate flush */
				if (rx_type != INFUSE_RPC_RSP) {
					/* Update the timeout */
					k_work_reschedule(&pending_flush_worker, K_MSEC(max_hold));
					K_SPINLOCK_BREAK;
				}
			}
			/* Cancel the pending flush timeout */
			k_work_cancel_delayable(&pending_flush_worker);
			/* Queue for transmission on backhaul */
			epacket_queue(backhaul, pending_buffer);
			pending_buffer = NULL;
		}
	}
	if (appended) {
		return;
	}

	/* No pending buffer, allocate one */
	temp = epacket_alloc_tx_for_interface(backhaul, K_FOREVER);

	K_SPINLOCK(&pending_lock) {
		if (epacket_received_packet_append(temp, buf) == 0) {
			pending_backhaul = backhaul;
			pending_buffer = temp;
			/* Initialise metadata */
			epacket_set_tx_metadata(pending_buffer, EPACKET_AUTH_DEVICE, 0x00,
						INFUSE_RECEIVED_EPACKET, EPACKET_ADDR_ALL);
			if (meta->type != INFUSE_RPC_RSP) {
				/* Start the flush timeout */
				k_work_reschedule(&pending_flush_worker, K_MSEC(max_hold));
			} else {
				/* Queue for transmission on backhaul immediately */
				epacket_queue(backhaul, pending_buffer);
				pending_buffer = NULL;
			}
		} else {
			/* Couldn't append to fresh buffer */
			LOG_WRN("Could not forward packet");
			net_buf_unref(temp);
			net_buf_unref(buf);
		}
	}
}

#else

static void receive_forward(const struct device *backhaul, struct net_buf *buf)
{
	struct net_buf *forward = epacket_alloc_tx_for_interface(backhaul, K_FOREVER);

#ifdef CONFIG_EPACKET_INTERFACE_BT_CENTRAL
	if (epacket_num_buffers_free_tx() <= CONFIG_EPACKET_RATE_LIMIT_BUFFER_THRESHOLD) {
		/* Running out of buffers, request a pause */
		LOG_DBG("Requesting rate limit");
		epacket_bt_gatt_rate_limit_request(CONFIG_EPACKET_RATE_LIMIT_REQ_DURATION_MS);
	}
#endif /* CONFIG_EPACKET_INTERFACE_BT_CENTRAL */

	if (epacket_received_packet_append(forward, buf) == 0) {
		/* Add metadata */
		epacket_set_tx_metadata(forward, EPACKET_AUTH_DEVICE, 0x00, INFUSE_RECEIVED_EPACKET,
					EPACKET_ADDR_ALL);
		/* Queue for transmission on backhaul */
		epacket_queue(backhaul, forward);
	} else {
		LOG_WRN("Could not forward packet");
		net_buf_unref(forward);
		net_buf_unref(buf);
	}
}

#endif /* CONFIG_EPACKET_RECEIVE_GROUPING */

void epacket_gateway_receive_handler(const struct device *backhaul, struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

#ifdef CONFIG_EPACKET_FORWARDING
	if (meta->interface == backhaul) {
		if ((meta->type == INFUSE_EPACKET_FORWARD) ||
		    (meta->type == INFUSE_EPACKET_FORWARD_AUTO_CONN)) {
#ifdef CONFIG_INFUSE_APPLICATION_STATES
			if (infuse_state_get(INFUSE_STATE_REBOOTING)) {
				/* Device is about to reboot, don't create more work */
				net_buf_unref(buf);
				return;
			}
#endif /* CONFIG_INFUSE_APPLICATION_STATES */
			epacket_packet_forward(buf);
			return;
		}
	}
#endif /* CONFIG_EPACKET_FORWARDING */

	/* Forward incoming Bluetooth packets */
	if ((meta->interface_id == EPACKET_INTERFACE_BT_ADV) ||
	    (meta->interface_id == EPACKET_INTERFACE_BT_CENTRAL)) {
		LOG_DBG("Received on %s: Auth=%d Type=%d Seq=%d Len=%d", meta->interface->name,
			meta->auth, meta->type, meta->sequence, buf->len);
#ifdef CONFIG_INFUSE_APPLICATION_STATES
		if (infuse_state_get(INFUSE_STATE_REBOOTING)) {
			/* Device is about to reboot, don't create more work */
			net_buf_unref(buf);
			return;
		}
#endif /* CONFIG_INFUSE_APPLICATION_STATES */
		receive_forward(backhaul, buf);
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
