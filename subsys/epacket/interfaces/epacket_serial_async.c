/**
 * @file
 * @copyright 2024 Embeint Inc
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
};

struct epacket_serial_data {
	struct epacket_interface_common_data common_data;
	const struct device *interface;
	struct net_buf *pending_tx;
	struct k_fifo tx_queue;
	uint8_t async_rx_buffer[2][CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_ASYNC_RX_BUFFER];
	volatile uint8_t async_rx_buffer_idx;
};

LOG_MODULE_REGISTER(epacket_serial, CONFIG_EPACKET_SERIAL_LOG_LEVEL);

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	const struct device *epacket_dev = user_data;
	struct epacket_serial_data *data = epacket_dev->data;
	struct net_buf *buf;
	int rc;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX_DONE: %p", data->pending_tx);
		/* Notify TX result */
		epacket_notify_tx_result(data->interface, data->pending_tx, 0);
		/* Free TX buffer */
		net_buf_unref(data->pending_tx);
		data->pending_tx = NULL;
		/* Release the serial port after a delay for transmission.
		 * 50ms is 720 bytes at 115200 bps.
		 */
		pm_device_runtime_put_async(dev, K_MSEC(50));
		/* Handle any queued buffers */
		buf = k_fifo_get(&data->tx_queue, K_NO_WAIT);
		if (buf != NULL) {
			rc = uart_tx(dev, buf->data, buf->len, 0);
			if (rc != 0) {
				LOG_ERR("ISR TX failed (%d)", rc);
				epacket_notify_tx_result(data->interface, buf, rc);
				net_buf_unref(buf);
			} else {
				data->pending_tx = buf;
			}
		}
		break;
	case UART_RX_BUF_REQUEST:
		rc = uart_rx_buf_rsp(dev, data->async_rx_buffer[data->async_rx_buffer_idx],
				     sizeof(data->async_rx_buffer[0]));
		__ASSERT_NO_MSG(rc == 0);
		data->async_rx_buffer_idx = !data->async_rx_buffer_idx ? 1 : 0;
		break;
	case UART_RX_RDY:
		LOG_DBG("RX_RDY: %p %d %d", (void *)evt->data.rx.buf, evt->data.rx.offset,
			evt->data.rx.len);
		/* Parse received data */
		epacket_serial_reconstruct(epacket_dev, evt->data.rx.buf + evt->data.rx.offset,
					   evt->data.rx.len, epacket_raw_receive_handler);
		break;
	case UART_RX_BUF_RELEASED:
		break;
	case UART_RX_DISABLED:
		break;
	default:
		LOG_WRN("Unhandled event: %d", evt->type);
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
		LOG_DBG("Failed to encrypt");
		rc = -EIO;
		goto error;
	}

	/* Push frame header on */
	header = net_buf_push(buf, sizeof(*header));
	*header = (struct epacket_serial_frame_header){
		.sync = {EPACKET_SERIAL_SYNC_A, EPACKET_SERIAL_SYNC_B},
		.len = buf->len - sizeof(*header),
	};

	/* Ensure serial port is powered up */
	rc = pm_device_runtime_get(config->backend);
	if (rc < 0) {
		rc = -ENODEV;
		goto error;
	}

	/* Queue packet for transmission */
	rc = uart_tx(config->backend, buf->data, buf->len, SYS_FOREVER_US);
	if (rc == -EBUSY) {
		LOG_DBG("Queuing buffer");
		k_fifo_put(&data->tx_queue, buf);
		return;
	} else if (rc != 0) {
		LOG_ERR("Failed to queue buffer (%d)", rc);
		pm_device_runtime_put(dev);
		goto error;
	}
	data->pending_tx = buf;
	return;
error:
	epacket_notify_tx_result(dev, buf, rc);
	net_buf_unref(buf);
}

static int epacket_receive_control(const struct device *dev, bool enable)
{
	const struct epacket_serial_config *config = dev->config;
	struct epacket_serial_data *data = dev->data;
	int rc;

	/* STM32 driver flushes the DMA when the line IDLE interrupt fires if timeout == 0,
	 * which is ideal. Otherwise 1 byte at 115200 baud is 8 uS, 500 uS should be a good
	 * timeout value.
	 */
	int32_t rx_timeout = IS_ENABLED(CONFIG_UART_STM32) ? 0 : 500;

	LOG_DBG("%d", enable);
	if (enable) {
		rc = pm_device_runtime_get(config->backend);
		if (rc == 0) {
			rc = uart_rx_enable(config->backend, data->async_rx_buffer[0],
					    sizeof(data->async_rx_buffer[0]), rx_timeout);
			data->async_rx_buffer_idx = 1;
		}
	} else {
		rc = uart_rx_disable(config->backend);
		(void)pm_device_runtime_put(config->backend);
	}

	return rc;
}

static int epacket_serial_init(const struct device *dev)
{
	const struct epacket_serial_config *config = dev->config;
	struct epacket_serial_data *data = dev->data;

	data->interface = dev;
	epacket_interface_common_init(dev);
	uart_callback_set(config->backend, uart_callback, (void *)dev);
	k_fifo_init(&data->tx_queue);
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
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epacket_serial_init, NULL, &serial_data_##inst,                \
			      &serial_config_##inst, POST_KERNEL, 0, &serial_api);

DT_INST_FOREACH_STATUS_OKAY(EPACKET_SERIAL_DEFINE)
