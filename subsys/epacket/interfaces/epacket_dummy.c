/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_dummy

LOG_MODULE_REGISTER(epacket_dummy, CONFIG_EPACKET_LOG_LEVEL);

static K_FIFO_DEFINE(epacket_dummy_fifo);

struct k_fifo *epacket_dummmy_transmit_fifo_get(void)
{
	return &epacket_dummy_fifo;
}

void epacket_dummy_receive(const struct device *dev, struct epacket_dummy_frame *header, uint8_t *payload,
			   size_t payload_len)
{
	struct net_buf *rx = epacket_alloc_rx(K_NO_WAIT);
	struct epacket_rx_metadata *meta = net_buf_user_data(rx);

	__ASSERT_NO_MSG(rx != NULL);

	/* Construct payload */
	net_buf_add_mem(rx, header, sizeof(*header));
	net_buf_add_mem(rx, payload, payload_len);

	meta->interface = dev;
	meta->interface_id = EPACKET_INTERFACE_DUMMY;
	meta->rssi = 0;

	/* Push at handling thread */
	epacket_raw_receive_handler(rx);
}

static void epacket_dummy_packet_overhead(const struct device *dev, size_t *header, size_t *footer)
{
	*header = sizeof(struct epacket_dummy_frame);
	*footer = 0;
}

static int epacket_dummy_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_dummy_frame *header = net_buf_push(buf, sizeof(struct epacket_dummy_frame));
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	header->type = meta->type;
	header->auth = meta->auth;
	header->flags = meta->flags;

	net_buf_put(&epacket_dummy_fifo, buf);
	return 0;
}

int epacket_dummy_decrypt(struct net_buf *buf)
{
	if (buf->len <= sizeof(struct epacket_dummy_frame)) {
		return -EINVAL;
	}
	struct epacket_dummy_frame *header = (void *)buf->data;
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));
	meta->auth = header->auth;
	meta->type = header->type;
	meta->flags = header->flags;
	meta->sequence = 0;
	return 0;
}

static int epacket_dummy_init(const struct device *dev)
{
	struct epacket_interface_common_data *data = dev->data;

	data->receive_handler = epacket_default_receive_handler;
	k_fifo_init(&epacket_dummy_fifo);
	return 0;
}

static const struct epacket_interface_api dummy_api = {
	.packet_overhead = epacket_dummy_packet_overhead,
	.send = epacket_dummy_send,
};

#define EPACKET_DUMMY_DEFINE(inst)                                                                                     \
	static struct epacket_interface_common_data epacket_dummy_data##inst;                                          \
	DEVICE_DT_INST_DEFINE(inst, epacket_dummy_init, NULL, &epacket_dummy_data##inst, NULL, POST_KERNEL, 0,         \
			      &dummy_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_DUMMY_DEFINE)
