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

#include <eis/epacket/interface.h>
#include <eis/epacket/packet.h>

#define DT_DRV_COMPAT embeint_epacket_usb

#define SYNC_A 0xD5
#define SYNC_B 0xCA

static const uint8_t sync_bytes[2] = {SYNC_A, SYNC_B};

struct serial_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

struct epacket_usb_config {
	const struct device *backend;
};

struct epacket_usb_data {
	struct k_fifo tx_fifo;
};

LOG_MODULE_REGISTER(epacket_usb, CONFIG_EPACKET_USB_LOG_LEVEL);

static void packet_reconstructor(const struct device *dev, uint8_t *buffer, size_t len)
{
	static struct net_buf *rx_buffer;
	static uint16_t payload_remaining;
	static uint8_t len_lsb;
	static uint16_t header_idx;

	for (int i = 0; i < len; i++) {
		switch (header_idx) {
		case 0:
		case 1:
			if (buffer[i] != sync_bytes[header_idx]) {
				header_idx = 0;
				continue;
			}
			break;
		case 2:
			len_lsb = buffer[i];
			break;
		case 3:
			payload_remaining = ((uint16_t)buffer[i] << 8) | len_lsb;
			LOG_DBG("%d byte packet incoming", payload_remaining);
			if (payload_remaining > CONFIG_EPACKET_PAYLOAD_MAX) {
				LOG_WRN("Sent payload too large (%d > %d)", payload_remaining,
					CONFIG_EPACKET_PAYLOAD_MAX);
			}
			break;
		}
		/* Still waiting on the payload length */
		if (header_idx <= 3) {
			header_idx++;
			continue;
		}
		/* Claim memory if none */
		if (rx_buffer == NULL) {
			rx_buffer = epacket_alloc_rx(K_FOREVER);
		}
		/* Payload bytes */
		uint16_t to_add = MIN(payload_remaining, len - i);

		/* Add payload to buffer */
		net_buf_add_mem(rx_buffer, buffer + i, to_add);
		payload_remaining -= to_add;
		i += to_add - 1;

		/* Is packet done? */
		if (payload_remaining == 0) {
			LOG_DBG("Packet received");
			/* Hand off to core ePacket functions */
			epacket_raw_receive_handler(dev, rx_buffer, 0);
			/* Reset parsing state */
			rx_buffer = NULL;
			header_idx = 0;
		}
	}
}

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
					packet_reconstructor(epacket_dev, buffer, recv_len);
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

			struct serial_header header = {.sync = {SYNC_A, SYNC_B}, .len = buf->len};

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

	k_fifo_init(&data->tx_fifo);
	uart_irq_callback_user_data_set(config->backend, interrupt_handler, (void *)dev);
	return 0;
}

static const struct epacket_interface_api usb_api = {
	.send = epacket_usb_send,
};

#define EPACKET_USB_DEFINE(inst)                                                                                       \
	static struct epacket_usb_data usb_data_##inst;                                                                \
	static const struct epacket_usb_config usb_config_##inst = {                                                   \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, cdc_acm)),                                                 \
	};                                                                                                             \
	DEVICE_DT_INST_DEFINE(inst, epacket_usb_init, NULL, &usb_data_##inst, &usb_config_##inst, POST_KERNEL, 0,      \
			      &usb_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_USB_DEFINE)
