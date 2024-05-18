/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static K_FIFO_DEFINE(handler_fifo);

static void custom_handler(struct net_buf *packet)
{
	net_buf_put(&handler_fifo, packet);
}

ZTEST(epacket_handlers, test_custom_handler)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct epacket_dummy_frame header = {
		.type = 0x10,
		.auth = EPACKET_AUTH_NETWORK,
		.flags = 0xAFFA,
	};
	struct epacket_rx_metadata *meta;
	uint8_t payload[16] = {0};
	struct net_buf *rx;

	/* Receive without a custom handler */
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	zassert_is_null(net_buf_get(&handler_fifo, K_MSEC(10)));

	/* Set the custom handler */
	epacket_set_receive_handler(epacket_dummy, custom_handler);

	/* Receive again with custom handler */
	epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	rx = net_buf_get(&handler_fifo, K_MSEC(10));
	zassert_not_null(rx);
	meta = net_buf_user_data(rx);

	/* Free the buffer */
	net_buf_unref(rx);
}

ZTEST(epacket_handlers, test_echo_response)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame *rx_header, header = {0};
	uint8_t payload[64] = {0};
	struct net_buf *rx;

	zassert_not_null(tx_fifo);

	/* Set the default handler */
	epacket_set_receive_handler(epacket_dummy, epacket_default_receive_handler);

	/* Send an echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_DEVICE;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	rx = net_buf_get(tx_fifo, K_MSEC(10));
	zassert_not_null(rx);
	rx_header = (void *)rx->data;
	zassert_equal(INFUSE_ECHO_RSP, rx_header->type);
	zassert_equal(EPACKET_AUTH_DEVICE, rx_header->auth);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 16, rx->len);
	net_buf_unref(rx);

	/* Send a different echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_NETWORK;
	epacket_dummy_receive(epacket_dummy, &header, payload, 64);

	rx = net_buf_get(tx_fifo, K_MSEC(10));
	zassert_not_null(rx);
	rx_header = (void *)rx->data;
	zassert_equal(INFUSE_ECHO_RSP, rx_header->type);
	zassert_equal(EPACKET_AUTH_NETWORK, rx_header->auth);
	zassert_equal(sizeof(struct epacket_dummy_frame) + 64, rx->len);
	net_buf_unref(rx);

	/* Send an auth failure echo request */
	header.type = INFUSE_ECHO_REQ;
	header.auth = EPACKET_AUTH_FAILURE;
	epacket_dummy_receive(epacket_dummy, &header, payload, 16);

	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(10)));
}

ZTEST(epacket_handlers, test_echo_no_block)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct epacket_dummy_frame header = {0};
	struct net_buf *tx;
	uint8_t payload[16] = {0};

	zassert_not_null(tx_fifo);

	/* Set the default handler */
	epacket_set_receive_handler(epacket_dummy, epacket_default_receive_handler);

	/* Multiple echo requests don't block */
	zassert_true(CONFIG_EPACKET_BUFFERS_RX > CONFIG_EPACKET_BUFFERS_TX);
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		header.type = INFUSE_ECHO_REQ;
		header.auth = EPACKET_AUTH_DEVICE;
		epacket_dummy_receive(epacket_dummy, &header, payload, sizeof(payload));
	}
	k_sleep(K_MSEC(1));
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		tx = net_buf_get(tx_fifo, K_MSEC(10));
		zassert_not_null(tx);
		net_buf_unref(tx);
	}
	zassert_is_null(net_buf_get(tx_fifo, K_MSEC(10)));
}

ZTEST(epacket_handlers, test_alloc_failure)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct net_buf *tx_bufs[CONFIG_EPACKET_BUFFERS_TX];
	struct net_buf *rx_bufs[CONFIG_EPACKET_BUFFERS_RX];

	/* Allocate all TX buffers, then check failure */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		tx_bufs[i] = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
		zassert_not_null(tx_bufs[i]);
	}
	zassert_is_null(epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT));

	/* Allocate all RX buffers, then check failure */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		rx_bufs[i] = epacket_alloc_rx(K_NO_WAIT);
		zassert_not_null(rx_bufs[i]);
	}
	zassert_is_null(epacket_alloc_rx(K_NO_WAIT));

	/* Free all buffers */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		net_buf_unref(tx_bufs[i]);
	}
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_RX; i++) {
		net_buf_unref(rx_bufs[i]);
	}
}

ZTEST_SUITE(epacket_handlers, NULL, NULL, NULL, NULL, NULL);
