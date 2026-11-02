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
};

struct epacket_serial_data {
	struct epacket_interface_common_data common_data;
	const struct device *interface;
	/* Buffer currently being transmitted, protected by tx_lock */
	struct net_buf *pending_tx;
	struct k_fifo tx_queue;
	struct k_spinlock tx_lock;
	atomic_t rx_enabled;
	/* Reception was stopped by the driver, only accessed from the UART callback */
	bool rx_stopped;
	uint8_t async_rx_buffer[2][CONFIG_EPACKET_INTERFACE_SERIAL_BACKEND_ASYNC_RX_BUFFER];
	volatile uint8_t async_rx_buffer_idx;
};

/* STM32 driver flushes the DMA when the line IDLE interrupt fires if timeout == 0,
 * which is ideal. Otherwise 1 byte at 115200 baud is 8 uS, 500 uS should be a good
 * timeout value.
 */
#define RX_TIMEOUT_US (IS_ENABLED(CONFIG_UART_STM32) ? 0 : 500)

LOG_MODULE_REGISTER(epacket_serial, CONFIG_EPACKET_SERIAL_LOG_LEVEL);

static int rx_start(const struct device *dev, struct epacket_serial_data *data)
{
	data->async_rx_buffer_idx = 1;
	return uart_rx_enable(dev, data->async_rx_buffer[0], sizeof(data->async_rx_buffer[0]),
			      RX_TIMEOUT_US);
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	const struct device *epacket_dev = user_data;
	struct epacket_serial_data *data = epacket_dev->data;
	k_spinlock_key_t key;
	struct net_buf *buf;
	int rc;

	switch (evt->type) {
	case UART_TX_DONE:
	case UART_TX_ABORTED:
		if (evt->type == UART_TX_DONE) {
			LOG_DBG("TX_DONE: %p", data->pending_tx);
			rc = 0;
		} else {
			LOG_DBG("TX_ABORTED: %p", data->pending_tx);
			rc = -EAGAIN;
		}
		buf = data->pending_tx;
		for (;;) {
			/* Notify TX result */
			epacket_notify_tx_result(data->interface, buf, rc);
			/* Free TX buffer */
			net_buf_unref(buf);
			/* Release the serial port after a delay for transmission.
			 * 50ms is 720 bytes at 115200 bps.
			 */
			pm_device_runtime_put_async(dev, K_MSEC(50));
			/* Start the next queued buffer, if any */
			key = k_spin_lock(&data->tx_lock);
			buf = k_fifo_get(&data->tx_queue, K_NO_WAIT);
			data->pending_tx = buf;
			k_spin_unlock(&data->tx_lock, key);
			if (buf == NULL) {
				break;
			}
			rc = uart_tx(dev, buf->data, buf->len, 0);
			if (rc == 0) {
				break;
			}
			/* Failed to start, complete it and try the next buffer */
			LOG_ERR("ISR TX failed (%d)", rc);
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
	case UART_RX_STOPPED:
		LOG_WRN("RX stopped (%d)", evt->data.rx_stop.reason);
		data->rx_stopped = true;
		break;
	case UART_RX_DISABLED:
		/* Restart reception if it was stopped by the driver (e.g. line error).
		 * Disables requested through `epacket_receive_control` don't generate
		 * `UART_RX_STOPPED`, and are restarted by the enable path instead.
		 */
		if (!data->rx_stopped) {
			break;
		}
		data->rx_stopped = false;
		if (atomic_get(&data->rx_enabled)) {
			/* Discard any partial frame interrupted by the error */
			epacket_serial_reconstruct_reset(epacket_dev);
			rc = rx_start(dev, data);
			if (rc < 0) {
				LOG_ERR("Failed to restart RX (%d)", rc);
			}
		}
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
	k_spinlock_key_t key;
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

	/* Queue the packet if a transmission is ongoing, the UART callback
	 * will start it once the current transmission completes.
	 */
	key = k_spin_lock(&data->tx_lock);
	if (data->pending_tx != NULL) {
		LOG_DBG("Queuing buffer");
		k_fifo_put(&data->tx_queue, buf);
		k_spin_unlock(&data->tx_lock, key);
		return;
	}
	/* Set before starting so that the UART callback can't see a NULL buffer */
	data->pending_tx = buf;
	k_spin_unlock(&data->tx_lock, key);

	rc = uart_tx(config->backend, buf->data, buf->len, SYS_FOREVER_US);
	if (rc != 0) {
		LOG_ERR("Failed to queue buffer (%d)", rc);
		/* No transmission ongoing, so the callback can't modify this. Only the
		 * ePacket TX thread calls this function, so no other packet can have
		 * been queued behind this buffer while the lock was released.
		 */
		data->pending_tx = NULL;
		pm_device_runtime_put(config->backend);
		goto error;
	}
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

	LOG_DBG("%d", enable);
	/* Only take or release the power reference on a state change */
	if (!atomic_cas(&data->rx_enabled, !enable, enable)) {
		return 0;
	}
	if (enable) {
		rc = pm_device_runtime_get(config->backend);
		if (rc == 0) {
			rc = rx_start(config->backend, data);
			if (rc < 0) {
				(void)pm_device_runtime_put(config->backend);
			}
		}
		if (rc < 0) {
			atomic_clear(&data->rx_enabled);
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
