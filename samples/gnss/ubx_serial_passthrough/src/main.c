/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net_buf.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/cfg.h>
#include <infuse/gnss/ubx/protocol.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

struct uart_passthrough_state {
	struct ubx_modem_data *modem;
	uint8_t async_rx_buffer[2][512];
	volatile uint8_t async_rx_buffer_idx;
	bool forwarding;
} state;

NET_BUF_POOL_DEFINE(to_ubx_pool, 4, 512, 0, NULL);
K_FIFO_DEFINE(to_ubx_fifo);
K_SEM_DEFINE(uart_tx_done, 0, 1);

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct net_buf *buf;
	int rc;

	switch (evt->type) {
	case UART_TX_DONE:
		k_sem_give(&uart_tx_done);
		break;
	case UART_RX_BUF_REQUEST:
		rc = uart_rx_buf_rsp(dev, state.async_rx_buffer[state.async_rx_buffer_idx],
				     sizeof(state.async_rx_buffer[0]));
		__ASSERT_NO_MSG(rc == 0);
		state.async_rx_buffer_idx = !state.async_rx_buffer_idx ? 1 : 0;
		break;
	case UART_RX_RDY:
		/* Parse received data */
		LOG_HEXDUMP_DBG(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len, "RX");

		buf = net_buf_alloc(&to_ubx_pool, K_NO_WAIT);
		if (buf == NULL) {
			LOG_ERR("No buffers remaining");
			return;
		}
		net_buf_add_mem(buf, evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
		k_fifo_put(&to_ubx_fifo, buf);
		break;
	case UART_RX_BUF_RELEASED:
		break;
	case UART_RX_DISABLED:
		break;
	default:
		LOG_WRN("Unhandled event: %d", evt->type);
	}
}

void ubx_modem_raw_frame_handler(struct ubx_frame *frame, uint16_t total_len)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(u_blox_serial));
	int rc;

	if (!state.forwarding) {
		return;
	}

	LOG_INF("UART<-UBX: %02x:%02x (%d bytes)", frame->message_class, frame->message_id,
		total_len);
	rc = uart_tx(uart, (void *)frame, total_len, SYS_FOREVER_US);
	if (rc != 0) {
		LOG_ERR("Failed to send (%d)", rc);
		return;
	}
	k_sem_take(&uart_tx_done, K_FOREVER);
}

int main(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(u_blox_serial));
	const struct device *gnss = DEVICE_DT_GET(DT_ALIAS(gnss));
	struct ubx_frame *frame;
	struct net_buf *buf;
	int rc;

	if (!device_is_ready(uart)) {
		LOG_ERR("UART %s not ready", uart->name);
		return -ENODEV;
	}
	if (!device_is_ready(gnss)) {
		LOG_ERR("GNSS %s not ready", gnss->name);
		return -ENODEV;
	}

	state.modem = ubx_modem_data_get(gnss);

	/* Power up UART and modem */
	rc = pm_device_runtime_get(uart);
	if (rc != 0) {
		LOG_ERR("Failed to request UART (%d)", rc);
		return rc;
	}
	rc = pm_device_runtime_get(gnss);
	if (rc != 0) {
		LOG_ERR("Failed to request GNSS (%d)", rc);
		return rc;
	}

	/* Configure RX */
	rc = uart_callback_set(uart, uart_callback, (void *)uart);
	if (rc != 0) {
		LOG_ERR("Failed to set callback (%d)", rc);
		return rc;
	}

	/* Permanently enable receiving */
	rc = uart_rx_enable(uart, state.async_rx_buffer[0], sizeof(state.async_rx_buffer[0]), 500);
	if (rc != 0) {
		LOG_ERR("Failed to enable RX (%d)", rc);
		return rc;
	}

	/* Enable forwarding of received frames */
	state.forwarding = true;

	for (;;) {
		buf = k_fifo_get(&to_ubx_fifo, K_FOREVER);

		rc = ubx_modem_send_async(state.modem, &buf->b, NULL, true);
		frame = (void *)buf->b.data;
		LOG_INF("UART->UBX: %02x:%02x (%d bytes)", frame->message_class, frame->message_id,
			buf->len);
		net_buf_unref(buf);
	}
	return 0;
}
