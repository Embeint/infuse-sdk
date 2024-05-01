/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

#include <infuse/epacket/interface.h>

LOG_MODULE_DECLARE(epacket);

void epacket_default_receive_handler(struct net_buf *buf)
{
	LOG_HEXDUMP_INF(buf->data, buf->len, "Received");
	net_buf_unref(buf);
}
