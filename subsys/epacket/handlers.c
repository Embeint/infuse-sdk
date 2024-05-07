/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/server.h>

LOG_MODULE_DECLARE(epacket);

void epacket_default_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_DBG("Received on %s: Auth=%d Type=%d Seq=%d Len=%d", meta->interface->name, meta->auth, meta->type,
		meta->sequence, buf->len);

	if (meta->auth == EPACKET_AUTH_FAILURE) {
		goto done;
	}

	if (meta->type == INFUSE_ECHO_REQ) {
		/* Respond to valid echo requests */
		struct net_buf *echo = epacket_alloc_tx_for_interface(meta->interface, K_NO_WAIT);

		if (echo == NULL) {
			LOG_WRN("Failed to allocate echo response");
		} else {
			epacket_set_tx_metadata(echo, meta->auth, 0, INFUSE_ECHO_RSP);
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
