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
	struct net_buf *tx, *sent;
	uint8_t payload[16];

	zassert_not_null(sent_queue);
	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_NETWORK, 0x00, 0x20);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate we can pick it up again */
	sent = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	zassert_equal(sent->len, sizeof(struct epacket_dummy_frame) + sizeof(payload));
	net_buf_unref(sent);
	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));
}

ZTEST_SUITE(epacket_dummy, NULL, NULL, NULL, NULL, NULL);
