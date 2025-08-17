/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_serial.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_serial

struct epacket_serial_config {
	struct epacket_interface_common_config common;
	const struct device *backend;
	bool backend_usb;
};

struct epacket_serial_data {
	struct epacket_interface_common_data common_data;
	struct k_work_delayable dc_handler;
	struct k_fifo tx_fifo;
	const struct device *interface;
#ifdef CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_INT_SINGLE_BYTE_SEND
	struct net_buf *pending;
#endif /* CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_INT_SINGLE_BYTE_SEND */
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
	const struct epacket_serial_config *config = data->interface->config;
	struct net_buf *buf;
	int cnt = 0;

	do {
		/* Drop any pending messages */
		buf = k_fifo_get(&data->tx_fifo, K_NO_WAIT);
		if (buf) {
			/* Notify TX result */
			epacket_notify_tx_result(data->interface, buf, -ETIMEDOUT);
			/* Release PM constraint */
			pm_device_runtime_put(config->backend);
			/* Free buffer */
			net_buf_unref(buf);
			cnt++;
		}
	} while (buf);
	LOG_DBG("Dropped %d packets", cnt);
}

static void uart_irq_rx_handle(const struct device *epacket_dev, const struct device *uart_dev)
{
	uint8_t buffer[64];
	int recv_len;

	do {
		/* Read the endpoint buffer size */
		recv_len = uart_fifo_read(uart_dev, buffer, sizeof(buffer));
		/* Extract ePacket packets */
		if (recv_len > 0) {
			epacket_serial_reconstruct(epacket_dev, buffer, recv_len,
						   epacket_raw_receive_handler);
		}
	} while (recv_len);
}

static void uart_irq_tx_handle(const struct device *epacket_dev, const struct device *uart_dev)
{
	struct epacket_serial_data *data = epacket_dev->data;
	int available;

	/* USB doesn't actually run from an interrupt */
	int key = irq_lock();

	available = uart_irq_tx_ready(uart_dev);
	if (available > 0) {
		/* Cancel the buffer flusher */
		k_work_cancel_delayable(&data->dc_handler);

#ifdef CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_INT_SINGLE_BYTE_SEND
		uint8_t next_byte;

		if (data->pending == NULL) {
			/* Pull next buffer to send */
			data->pending = k_fifo_get(&data->tx_fifo, K_NO_WAIT);
			if (data->pending == NULL) {
				uart_irq_tx_disable(uart_dev);
				irq_unlock(key);
				return;
			}
		}
		/* Push next byte onto FIFO */
		next_byte = net_buf_pull_u8(data->pending);
		uart_fifo_fill(uart_dev, &next_byte, 1);

		if (data->pending->len == 0) {
			pm_device_runtime_put_async(uart_dev, K_MSEC(50));
			epacket_notify_tx_result(data->interface, data->pending, 0);
			net_buf_unref(data->pending);
			data->pending = NULL;
		}
#else
		struct net_buf *buf;
		int sent, required;

		/* Only need to push if we have a packet */
		buf = k_fifo_get(&data->tx_fifo, K_NO_WAIT);
		if (buf == NULL) {
			uart_irq_tx_disable(uart_dev);
			irq_unlock(key);
			return;
		}

		required = buf->len;
		if (available < required) {
			LOG_WRN("Insufficient buffer space");
			k_fifo_put(&data->tx_fifo, buf);
			uart_irq_tx_disable(uart_dev);
			irq_unlock(key);
			/* Reschedule the buffer flusher */
			k_work_reschedule(&data->dc_handler, K_MSEC(100));
			return;
		}

		/* Push payload */
		sent = uart_fifo_fill(uart_dev, buf->data, buf->len);
		if (sent != buf->len) {
			/* Should be impossible with the IRQ lock and previous checks */
			LOG_ERR("FIFO fail? %d != %d", sent, buf->len);
		}

		/* Notify TX result */
		epacket_notify_tx_result(data->interface, buf, 0);

		/* Free TX buffer */
		net_buf_unref(buf);

		/* Release the serial port after a delay for transmission.
		 * 50ms is 720 bytes at 115200 bps.
		 */
		pm_device_runtime_put_async(uart_dev, K_MSEC(50));

		LOG_DBG("sent %d/%d", sent, available);
#endif /* CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_INT_SINGLE_BYTE_SEND */
	}
	irq_unlock(key);
}

static void interrupt_handler(const struct device *dev, void *user_data)
{
	const struct device *epacket_dev = user_data;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uart_irq_rx_handle(epacket_dev, dev);
		}
		uart_irq_tx_handle(epacket_dev, dev);
	}
}

static void epacket_serial_send(const struct device *dev, struct net_buf *buf)
{
	const struct epacket_serial_config *config = dev->config;
	struct epacket_serial_data *data = dev->data;
	struct epacket_serial_frame_header *header;
	int rc;

	/* Encrypt the payload */
	if (epacket_serial_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		net_buf_unref(buf);
		return;
	}

	/* Push frame header on */
	header = net_buf_push(buf, sizeof(*header));
	*header = (struct epacket_serial_frame_header){
		.sync = {EPACKET_SERIAL_SYNC_A, EPACKET_SERIAL_SYNC_B},
		.len = buf->len - sizeof(*header),
	};

	/* Power up serial port */
	rc = pm_device_runtime_get(config->backend);
	if (rc < 0) {
		epacket_notify_tx_result(dev, buf, -ENODEV);
		net_buf_unref(buf);
		return;
	}

	/* Push packet onto queue */
	k_fifo_put(&data->tx_fifo, buf);

	/* Driver has 100ms to queue the packet or it will be dropped */
	k_work_reschedule(&data->dc_handler, K_MSEC(100));

	/* Enable interrupt to trigger send */
	uart_irq_tx_enable(config->backend);
}

static int epacket_receive_control(const struct device *dev, bool enable)
{
	const struct epacket_serial_config *config = dev->config;
	int rc;

	/* USB backend is always enabled */
	if (config->backend_usb) {
		return 0;
	}

	if (enable) {
		rc = pm_device_runtime_get(config->backend);
		if (rc < 0) {
			return rc;
		}
		uart_irq_rx_enable(config->backend);
	} else {
		uart_irq_rx_disable(config->backend);
		(void)pm_device_runtime_put(config->backend);
	}
	return 0;
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
	/* Enabling RX has no cost on USB */
	if (config->backend_usb) {
		uart_irq_rx_enable(config->backend);
	}
	return 0;
}

static const struct epacket_interface_api serial_api = {
	.send = epacket_serial_send,
	.receive_ctrl = epacket_receive_control,
};

#define EPACKET_SERIAL_DEFINE(inst)                                                                \
	BUILD_ASSERT(sizeof(struct epacket_serial_frame_header) +                                  \
			     sizeof(struct epacket_serial_frame) ==                                \
		     DT_INST_PROP(inst, header_size));                                             \
	static struct epacket_serial_data serial_data_##inst;                                      \
	static const struct epacket_serial_config serial_config_##inst = {                         \
		.common =                                                                          \
			{                                                                          \
				.max_packet_size =                                                 \
					EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(inst)),           \
				.header_size = DT_INST_PROP(inst, header_size),                    \
				.footer_size = DT_INST_PROP(inst, footer_size),                    \
			},                                                                         \
		.backend = DEVICE_DT_GET(DT_INST_PROP(inst, serial)),                              \
		.backend_usb =                                                                     \
			DT_NODE_HAS_COMPAT(DT_INST_PROP(inst, serial), zephyr_cdc_acm_uart),       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epacket_serial_init, NULL, &serial_data_##inst,                \
			      &serial_config_##inst, POST_KERNEL, 0, &serial_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_SERIAL_DEFINE)
