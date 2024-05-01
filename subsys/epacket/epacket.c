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

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface.h>

#include "interfaces/epacket_internal.h"

NET_BUF_POOL_DEFINE(epacket_scratch, 1, CONFIG_EPACKET_PAYLOAD_MAX, 0, NULL);
NET_BUF_POOL_DEFINE(epacket_pool_tx, CONFIG_EPACKET_BUFFERS_TX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);
NET_BUF_POOL_DEFINE(epacket_pool_rx, CONFIG_EPACKET_BUFFERS_RX, CONFIG_EPACKET_PAYLOAD_MAX,
		    sizeof(struct epacket_metadata), NULL);

static K_FIFO_DEFINE(epacket_rx_queue);
static K_FIFO_DEFINE(epacket_tx_queue);
static struct epacket_receive_metadata rx_metadata[CONFIG_EPACKET_BUFFERS_RX];
static const struct device *tx_device[CONFIG_EPACKET_BUFFERS_TX];

LOG_MODULE_REGISTER(epacket, CONFIG_EPACKET_LOG_LEVEL);

struct net_buf *epacket_encryption_scratch(void)
{
	return net_buf_alloc(&epacket_scratch, K_FOREVER);
}

struct net_buf *epacket_alloc_tx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_tx, timeout);
}

struct net_buf *epacket_alloc_rx(k_timeout_t timeout)
{
	return net_buf_alloc(&epacket_pool_rx, timeout);
}

void epacket_queue(const struct device *dev, struct net_buf *buf)
{
	/* Store transmit device */
	tx_device[net_buf_id(buf)] = dev;

	/* Push packet at processing queue */
	net_buf_put(&epacket_tx_queue, buf);
}

void epacket_raw_receive_handler(struct epacket_receive_metadata *metadata, struct net_buf *buf)
{
	/* Store metadata */
	rx_metadata[net_buf_id(buf)] = *metadata;

	/* Push packet at processing queue */
	net_buf_put(&epacket_rx_queue, buf);
}

static void epacket_handle_rx(struct net_buf *buf)
{
	struct epacket_interface_common_data *interface_data;
	struct epacket_receive_metadata *metadata;
	uint16_t sequence = 0;
	int rc;

	metadata = &rx_metadata[net_buf_id(buf)];
	interface_data = metadata->interface->data;

	LOG_WRN("%s: received %d byte packet (%d dBm)", metadata->interface->name, buf->len, metadata->rssi);

	/* Payload decoding */
	switch (metadata->interface_id) {
#ifdef CONFIG_EPACKET_INTERFACE_SERIAL
	case EPACKET_INTERFACE_SERIAL:
		rc = epacket_serial_decrypt(buf, &sequence);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_SERIAL */
#ifdef CONFIG_EPACKET_INTERFACE_UDP
	case EPACKET_INTERFACE_UDP:
		rc = epacket_udp_decrypt(buf, &sequence);
		break;
#endif /* CONFIG_EPACKET_INTERFACE_UDP */
	default:
		LOG_WRN("Unknown interface ID %d", metadata->interface_id);
		rc = -1;
	}
	metadata->sequence = sequence;
	LOG_DBG("Decrypt result: %d", rc);

	/* Payload handling */
	interface_data->receive_handler(metadata, buf);
}

static void epacket_handle_tx(struct net_buf *buf)
{
	const struct epacket_interface_api *api;
	const struct device *dev;
	size_t pool_max;
	int pool_id;
	int rc;

	pool_id = net_buf_id(buf);
	dev = tx_device[pool_id];
	api = dev->api;
	pool_max = net_buf_pool_get(pool_id)->alloc->max_alloc_size;

	/* Reverse any footer reservation that was done at allocation */
	if (buf->size < pool_max) {
		buf->size = pool_max;
	}

	LOG_DBG("%s: TX %d byte packet", dev->name, buf->len);
	rc = api->send(dev, buf);
	/* TODO: Notify interested parties of TX failures */
	(void)rc;
}

static int epacket_processor(void *a, void *b, void *c)
{
	struct k_poll_event events[2] = {
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
						&epacket_rx_queue, 0),
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
						&epacket_tx_queue, 0),
	};

	struct net_buf *buf;
	int rc;

	while (true) {
		rc = k_poll(events, ARRAY_SIZE(events), K_FOREVER);
		if (rc != 0) {
			LOG_WRN("Processor timeout (%d)", rc);
			continue;
		}

		if (events[0].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = net_buf_get(events[0].fifo, K_NO_WAIT);
			epacket_handle_rx(buf);
			events[0].state = K_POLL_STATE_NOT_READY;
		}

		if (events[1].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
			buf = net_buf_get(events[1].fifo, K_NO_WAIT);
			epacket_handle_tx(buf);
			events[1].state = K_POLL_STATE_NOT_READY;
		}
	}
	return 0;
}

K_THREAD_DEFINE(epacket_processor_thread, 2048, epacket_processor, NULL, NULL, NULL, 0, K_ESSENTIAL, 0);
