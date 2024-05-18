/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static struct epacket_interface_cb interface_cb;
static K_SEM_DEFINE(tx_failed, 0, 1);
static int failure_rc;

static void tx_failed_cb(const struct net_buf *buf, int reason, void *user_ctx)
{
	failure_rc = reason;
	k_sem_give(&tx_failed);
}

ZTEST(epacket_callbacks, test_tx_failure)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *tx, *sent;
	uint8_t payload[16];

	zassert_not_null(sent_queue);
	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));

	interface_cb.tx_failure = tx_failed_cb;

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it was sent properly, no callback */
	sent = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	zassert_equal(-EAGAIN, k_sem_take(&tx_failed, K_MSEC(1)));
	net_buf_unref(sent);

	/* Set interface to fail to send */
	epacket_dummy_set_tx_failure(-ENOTCONN);

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it wasn't sent properly, callback received */
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));
	zassert_equal(0, k_sem_take(&tx_failed, K_MSEC(1)));
	zassert_equal(-ENOTCONN, failure_rc);

	/* Reset interface failures */
	epacket_dummy_set_tx_failure(0);
}

void *callback_setup(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_register_callback(epacket_dummy, &interface_cb);

	return NULL;
}

ZTEST_SUITE(epacket_callbacks, NULL, callback_setup, NULL, NULL, NULL);
