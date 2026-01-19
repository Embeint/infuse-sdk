/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
static int send_error_code;
static uint16_t max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0));
static bool receiving;
static int receive_rc;

void epacket_dummy_reset_callbacks(const struct device *dev)
{
	struct epacket_interface_common_data *data = dev->data;

	sys_slist_init(&data->callback_list);
}

struct k_fifo *epacket_dummmy_transmit_fifo_get(void)
{
	return &epacket_dummy_fifo;
}

void epacket_dummy_set_tx_failure(int error_code)
{
	send_error_code = error_code;
}

bool epacket_dummy_receive_scheduled(void)
{
	return receiving;
}

void epacket_dummy_set_max_packet(uint16_t packet_size)
{
	max_packet_size = MIN(packet_size, EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)));
}

void epacket_dummy_set_interface_state(const struct device *dev, bool state)
{
	const struct epacket_interface_common_config *config = dev->config;
	struct epacket_interface_common_data *data = dev->data;
	struct epacket_interface_cb *cb;
	uint16_t size = max_packet_size - config->header_size - config->footer_size;

	SYS_SLIST_FOR_EACH_CONTAINER(&data->callback_list, cb, node) {
		if (cb->interface_state) {
			cb->interface_state(state ? size : 0, cb->user_ctx);
		}
	}
}

void epacket_dummy_receive_extra(const struct device *dev, const struct epacket_dummy_frame *header,
				 const void *payload, size_t payload_len, const void *extra,
				 size_t extra_len)
{
	struct net_buf *rx = epacket_alloc_rx(K_FOREVER);
	struct epacket_rx_metadata *meta = net_buf_user_data(rx);

	__ASSERT_NO_MSG(rx != NULL);

	meta->interface = dev;
	meta->interface_id = EPACKET_INTERFACE_DUMMY;
	meta->rssi = 0;

	/* Construct payload */
	if (header != NULL) {
		net_buf_add_mem(rx, header, sizeof(*header));
	}
	net_buf_add_mem(rx, payload, payload_len);
	if (extra_len > 0) {
		net_buf_add_mem(rx, extra, extra_len);
	}

	/* Push at handling thread */
	epacket_raw_receive_handler(rx);
}

static void epacket_dummy_send(const struct device *dev, struct net_buf *buf)
{
	struct epacket_dummy_frame *header = net_buf_push(buf, sizeof(struct epacket_dummy_frame));
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);
	int error_code = send_error_code;

	if (max_packet_size == 0) {
		error_code = -ENOTCONN;
	}

	header->type = meta->type;
	header->auth = meta->auth;
	header->flags = meta->flags;
	header->key_identifier = meta->key_identifier;

	epacket_notify_tx_result(dev, buf, error_code);
	if (error_code == 0) {
		k_fifo_put(&epacket_dummy_fifo, buf);
	} else {
		net_buf_unref(buf);
	}
}

static int epacket_dummy_receive_ctrl(const struct device *dev, bool enable)
{
	ARG_UNUSED(dev);

	if (receive_rc == 0) {
		receiving = enable;
	}
	return receive_rc;
}

uint16_t epacket_dummy_max_packet_size(const struct device *dev)
{
	return max_packet_size;
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
	meta->key_identifier = header->key_identifier;
	meta->sequence = 0;
	return 0;
}

static int epacket_dummy_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	k_fifo_init(&epacket_dummy_fifo);
	return 0;
}

static struct epacket_interface_api dummy_api = {
	.send = epacket_dummy_send,
	.receive_ctrl = epacket_dummy_receive_ctrl,
	.max_packet_size = epacket_dummy_max_packet_size,
};

void epacket_dummy_receive_api_override(bool func_exists, int rc)
{
	if (func_exists) {
		dummy_api.receive_ctrl = epacket_dummy_receive_ctrl;
	} else {
		dummy_api.receive_ctrl = NULL;
	}
	receive_rc = rc;
}

#define EPACKET_DUMMY_DEFINE(inst)                                                                 \
	BUILD_ASSERT(sizeof(struct epacket_dummy_frame) == DT_INST_PROP(inst, header_size));       \
	static struct epacket_interface_common_data epacket_dummy_data##inst;                      \
	static const struct epacket_interface_common_config epacket_dummy_config##inst = {         \
		.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(inst)),                \
		.header_size = DT_INST_PROP(inst, header_size),                                    \
		.footer_size = DT_INST_PROP(inst, footer_size),                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epacket_dummy_init, NULL, &epacket_dummy_data##inst,           \
			      &epacket_dummy_config##inst, POST_KERNEL, 0, &dummy_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_DUMMY_DEFINE)
