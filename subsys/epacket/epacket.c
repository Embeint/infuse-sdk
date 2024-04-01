/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

#include <eis/epacket/packet.h>

NET_BUF_POOL_DEFINE(epacket_pool_tx, CONFIG_EPACKET_BUFFERS_TX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);
NET_BUF_POOL_DEFINE(epacket_pool_rx, CONFIG_EPACKET_BUFFERS_RX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);

LOG_MODULE_REGISTER(epacket, CONFIG_EPACKET_LOG_LEVEL);

struct net_buf *epacket_alloc_tx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_tx, timeout);
}

struct net_buf *epacket_alloc_rx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_rx, timeout);
}

void epacket_raw_receive_handler(const struct device *dev, struct net_buf *buf, uint8_t rssi)
{
	LOG_WRN("%s: received %d byte packet (%d dBm)", dev->name, buf->len, -(int16_t)rssi);

	net_buf_unref(buf);
}
