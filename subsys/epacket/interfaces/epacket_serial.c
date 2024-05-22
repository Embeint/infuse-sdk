/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_serial.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_serial

struct serial_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

struct epacket_serial_config {
	struct epacket_interface_common_config common;
	const struct device *backend;
};

struct epacket_serial_data {
	struct epacket_interface_common_data common_data;
	struct k_work_delayable dc_handler;
	struct k_fifo tx_fifo;
	const struct device *interface;
};

LOG_MODULE_REGISTER(epacket_serial, CONFIG_EPACKET_SERIAL_LOG_LEVEL);

/* For USB, there is no way of knowing whether a device is on the other end
 * triggering the transmission of the ePackets we are queuing. To avoid
 * queuing all our TX buffers and then blocking the system, we give the
 * system up to 100ms to start the transmission of a packet. If that timeout
 * expires, purge the queue instead.
 */
static void disconnected_handler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct epacket_serial_data *data =
		CONTAINER_OF(delayable, struct epacket_serial_data, dc_handler);
	struct net_buf *buf;
	int cnt = 0;

	do {
		/* Drop any pending messages */
		buf = net_buf_get(&data->tx_fifo, K_NO_WAIT);
		if (buf) {
			/* Notify TX result */
			epacket_notify_tx_result(data->interface, buf, -ETIMEDOUT);
			/* Free buffer */
			net_buf_unref(buf);
			cnt++;
		}
	} while (buf);
	LOG_DBG("Dropped %d packets", cnt);
}

static void interrupt_handler(const struct device *dev, void *user_data)
{
	const struct device *epacket_dev = user_data;
	struct epacket_serial_data *data = epacket_dev->data;
	struct net_buf *buf;
	int sent, required, available;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buffer[64];
			int recv_len;

			do {
				/* Read the endpoint buffer size */
				recv_len = uart_fifo_read(dev, buffer, sizeof(buffer));
				/* Extract ePacket packets */
				if (recv_len > 0) {
					epacket_serial_reconstruct(epacket_dev, buffer, recv_len,
								   epacket_raw_receive_handler);
				}
			} while (recv_len);
		}

		/* USB doesn't actually run from an interrupt */
		int key = irq_lock();

		available = uart_irq_tx_ready(dev);
		if (available > 0) {
			/* Cancel the buffer flusher */
			k_work_cancel_delayable(&data->dc_handler);
			/* Only need to push if we have a packet */
			buf = net_buf_get(&data->tx_fifo, K_NO_WAIT);
			if (buf == NULL) {
				uart_irq_tx_disable(dev);
				irq_unlock(key);
				return;
			}

			required = buf->len;
			if (available < required) {
				LOG_WRN("Insufficient buffer space");
				net_buf_put(&data->tx_fifo, buf);
				uart_irq_tx_disable(dev);
				irq_unlock(key);
				return;
			}

			/* Push payload */
			sent = uart_fifo_fill(dev, buf->data, buf->len);

			/* Notify TX result */
			epacket_notify_tx_result(data->interface, buf, 0);

			/* Free TX buffer */
			net_buf_unref(buf);

			LOG_DBG("sent %d/%d", sent, available);
		}
		irq_unlock(key);
	}
}

static void epacket_serial_send(const struct device *dev, struct net_buf *buf)
{
	const struct epacket_serial_config *config = dev->config;
	struct epacket_serial_data *data = dev->data;
	struct epacket_serial_frame_header *header;

	/* Encrypt the payload */
	if (epacket_serial_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(data->interface, buf, -EIO);
		net_buf_unref(buf);
		return;
	}

	/* Push frame header on */
	header = net_buf_push(buf, sizeof(*header));
	*header = (struct epacket_serial_frame_header){
		.sync = {EPACKET_SERIAL_SYNC_A, EPACKET_SERIAL_SYNC_B},
		.len = buf->len - sizeof(*header),
	};

	/* Push packet onto queue */
	net_buf_put(&data->tx_fifo, buf);

	/* Driver has 100ms to queue the packet or it will be dropped */
	k_work_reschedule(&data->dc_handler, K_MSEC(100));

	/* Enable interrupt to trigger send */
	uart_irq_tx_enable(config->backend);
}

static int epacket_serial_init(const struct device *dev)
{
	const struct epacket_serial_config *config = dev->config;
	struct epacket_serial_data *data = dev->data;

	data->interface = dev;
	epacket_interface_common_init(dev);
	k_work_init_delayable(&data->dc_handler, disconnected_handler);
	k_fifo_init(&data->tx_fifo);
	uart_irq_callback_user_data_set(config->backend, interrupt_handler, (void *)dev);
	uart_irq_rx_enable(config->backend);
	return 0;
}

static const struct epacket_interface_api serial_api = {
	.send = epacket_serial_send,
};

#define EPACKET_SERIAL_DEFINE(inst)                                                                \
	BUILD_ASSERT(sizeof(struct epacket_serial_frame_header) +                                  \
			     sizeof(struct epacket_serial_frame) ==                                \
		     DT_INST_PROP(inst, header_size));                                             \
	static struct epacket_serial_data serial_data_##inst;                                      \
	static const struct epacket_serial_config serial_config_##inst = {                         \
		.common =                                                                          \
			{                                                                          \
				.header_size = DT_INST_PROP(inst, header_size),                    \
				.footer_size = DT_INST_PROP(inst, footer_size),                    \
			},                                                                         \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, serial)),                              \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epacket_serial_init, NULL, &serial_data_##inst,                \
			      &serial_config_##inst, POST_KERNEL, 0, &serial_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_SERIAL_DEFINE)
