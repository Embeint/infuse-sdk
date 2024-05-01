/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

ZTEST(epacket_dummy, test_send_queue)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *tx, *sent, *rx;
	uint8_t payload[16];
	int rc;

	zassert_not_null(sent_queue);
	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate we can pick it up again */
	sent = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	zassert_equal(sent->len, sizeof(struct epacket_dummy_frame) + sizeof(payload));

	/* Copy message contents across to RX buffer */
	rx = epacket_alloc_rx(K_NO_WAIT);
	zassert_not_null(rx);
	net_buf_add_mem(rx, sent->data, sent->len);
	net_buf_unref(sent);

	/* Decrypt and validate against original packet */
	rc = epacket_dummy_decrypt(rx);
	zassert_equal(0, rc);
	rx_meta = net_buf_user_data(rx);
	zassert_equal(EPACKET_AUTH_DEVICE, rx_meta->auth);
	zassert_equal(0x20, rx_meta->type);
	zassert_equal(0x1234, rx_meta->flags);
	zassert_equal(0, rx_meta->sequence);
	net_buf_unref(rx);

	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));
}

ZTEST_SUITE(epacket_dummy, NULL, NULL, NULL, NULL, NULL);
