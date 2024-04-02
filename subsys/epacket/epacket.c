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
#include <eis/epacket/interface.h>

NET_BUF_POOL_DEFINE(epacket_pool_tx, CONFIG_EPACKET_BUFFERS_TX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);
NET_BUF_POOL_DEFINE(epacket_pool_rx, CONFIG_EPACKET_BUFFERS_RX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);

static K_FIFO_DEFINE(epacket_process_queue);
static struct epacket_receive_metadata rx_metadata[CONFIG_EPACKET_BUFFERS_RX];

LOG_MODULE_REGISTER(epacket, CONFIG_EPACKET_LOG_LEVEL);

struct net_buf *epacket_alloc_tx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_tx, timeout);
}

struct net_buf *epacket_alloc_rx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_rx, timeout);
}

void epacket_raw_receive_handler(struct epacket_receive_metadata *metadata, struct net_buf *buf)
{
	/* Store metadata */
	rx_metadata[net_buf_id(buf)] = *metadata;

	/* Push packet at processing queue */
	net_buf_put(&epacket_process_queue, buf);
}

int epacket_processor(void *a, void *b, void *c)
{
	struct epacket_receive_metadata *metadata;
	struct net_buf *buf;

	while (true) {
		buf = net_buf_get(&epacket_process_queue, K_FOREVER);
		metadata = &rx_metadata[net_buf_id(buf)];

		LOG_WRN("%s: received %d byte packet (%d dBm)", metadata->interface->name, buf->len, metadata->rssi);

		/* Payload decoding */
		switch (metadata->interface_id) {
		case EPACKET_INTERFACE_SERIAL:
			break;
		default:
			LOG_WRN("Unknown interface ID %d", metadata->interface_id);
		}

		/* Payload handling */
		net_buf_unref(buf);
	}
}

K_THREAD_DEFINE(epacket_processor_thread, 1024, epacket_processor, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
