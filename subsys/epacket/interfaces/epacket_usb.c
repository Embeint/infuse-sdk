/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
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

#define DT_DRV_COMPAT embeint_epacket_usb

struct serial_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

struct epacket_usb_config {
	struct epacket_interface_common_config common;
	const struct device *backend;
};

struct epacket_usb_data {
	struct epacket_interface_common_data common_data;
	struct k_fifo tx_fifo;
};

LOG_MODULE_REGISTER(epacket_usb, CONFIG_EPACKET_USB_LOG_LEVEL);

static void interrupt_handler(const struct device *dev, void *user_data)
{
	const struct device *epacket_dev = user_data;
	struct epacket_usb_data *data = epacket_dev->data;
	struct net_buf *buf;
	int sent, required, available;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buffer[CONFIG_CDC_ACM_BULK_EP_MPS];
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
			/* Only need to push if we*/
			buf = net_buf_get(&data->tx_fifo, K_NO_WAIT);
			if (buf == NULL) {
				irq_unlock(key);
				return;
			}

			required = sizeof(struct serial_header) + buf->len;
			if (available < required) {
				LOG_WRN("Insufficient buffer space");
				net_buf_put(&data->tx_fifo, buf);
				irq_unlock(key);
				return;
			}

			struct serial_header header = {.sync = {SERIAL_SYNC_A, SERIAL_SYNC_B}, .len = buf->len};

			/* Push header */
			sent = uart_fifo_fill(dev, (const uint8_t *)&header, sizeof(header));
			/* Push payload */
			sent += uart_fifo_fill(dev, buf->data, buf->len);

			/* Free TX buffer */
			net_buf_unref(buf);

			LOG_DBG("sent %d/%d", sent, available);
		}
		irq_unlock(key);
	}
}

static int epacket_usb_send(const struct device *dev, struct net_buf *buf)
{
	const struct epacket_usb_config *config = dev->config;
	struct epacket_usb_data *data = dev->data;

	/* Encrypt the payload */
	if (epacket_serial_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		net_buf_unref(buf);
		return -EIO;
	}

	/* Push packet onto queue */
	net_buf_put(&data->tx_fifo, buf);

	/* Enable interrupt to trigger send */
	uart_irq_tx_enable(config->backend);
	return 0;
}

static int epacket_usb_init(const struct device *dev)
{
	const struct epacket_usb_config *config = dev->config;
	struct epacket_usb_data *data = dev->data;

	data->common_data.receive_handler = epacket_default_receive_handler;
	k_fifo_init(&data->tx_fifo);
	uart_irq_callback_user_data_set(config->backend, interrupt_handler, (void *)dev);
	uart_irq_rx_enable(config->backend);
	return 0;
}

static const struct epacket_interface_api usb_api = {
	.send = epacket_usb_send,
};

#define EPACKET_USB_DEFINE(inst)                                                                                       \
	BUILD_ASSERT(sizeof(struct epacket_serial_frame) == DT_INST_PROP(inst, header_size));                          \
	static struct epacket_usb_data usb_data_##inst;                                                                \
	static const struct epacket_usb_config usb_config_##inst = {                                                   \
		.common =                                                                                              \
			{                                                                                              \
				.header_size = DT_INST_PROP(inst, header_size),                                        \
				.footer_size = DT_INST_PROP(inst, footer_size),                                        \
			},                                                                                             \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, cdc_acm)),                                                 \
	};                                                                                                             \
	DEVICE_DT_INST_DEFINE(inst, epacket_usb_init, NULL, &usb_data_##inst, &usb_config_##inst, POST_KERNEL, 0,      \
			      &usb_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_USB_DEFINE)
