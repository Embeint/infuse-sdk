/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

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
	uint8_t payload[16];
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

ZTEST_SUITE(epacket_handlers, NULL, NULL, NULL, NULL, NULL);
