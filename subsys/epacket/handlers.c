/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

LOG_MODULE_DECLARE(epacket);

void epacket_default_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_INF("Received on %s: Auth=%d Type=%d Seq=%d Len=%d", meta->interface->name, meta->auth, meta->type,
		meta->sequence, buf->len);
	net_buf_unref(buf);
}
