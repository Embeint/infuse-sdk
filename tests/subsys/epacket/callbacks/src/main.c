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
static struct k_poll_signal tx_fail_signal;
static struct k_poll_signal tx_done_signal;

static void tx_failed_cb(const struct net_buf *buf, int reason, void *user_ctx)
{
	zassert_not_null(buf);
	zassert_equal(user_ctx, &interface_cb);

	k_poll_signal_raise(&tx_fail_signal, reason);
}

static void tx_done(const struct device *dev, struct net_buf *buf, int result)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	zassert_equal(epacket_dummy, dev);
	zassert_not_null(buf);

	k_poll_signal_raise(&tx_done_signal, result);
}

ZTEST(epacket_callbacks, test_interface_tx_failure)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *tx, *sent;
	uint8_t payload[16];
	int signaled, result;

	zassert_not_null(sent_queue);
	zassert_is_null(net_buf_get(sent_queue, K_NO_WAIT));

	interface_cb.tx_failure = tx_failed_cb;
	interface_cb.user_ctx = &interface_cb;

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20);
	epacket_set_tx_callback(tx, tx_done);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it was sent properly */
	sent = net_buf_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	net_buf_unref(sent);
	/* No interface failure callback */
	k_poll_signal_check(&tx_fail_signal, &signaled, &result);
	zassert_equal(0, signaled);
	/* TX done callback */
	k_poll_signal_check(&tx_done_signal, &signaled, &result);
	zassert_equal(1, signaled);
	zassert_equal(0, result);
	k_poll_signal_reset(&tx_done_signal);

	/* Set interface to fail to send */
	epacket_dummy_set_tx_failure(-ENOTCONN);

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20);
	epacket_set_tx_callback(tx, tx_done);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it wasn't sent properly, callback received */
	zassert_is_null(net_buf_get(sent_queue, K_MSEC(1)));
	/* Interface failure callback */
	k_poll_signal_check(&tx_fail_signal, &signaled, &result);
	zassert_equal(1, signaled);
	zassert_equal(-ENOTCONN, result);
	k_poll_signal_reset(&tx_fail_signal);
	/* TX done callback */
	k_poll_signal_check(&tx_done_signal, &signaled, &result);
	zassert_equal(1, signaled);
	zassert_equal(-ENOTCONN, result);
	k_poll_signal_reset(&tx_done_signal);

	/* Reset interface failures */
	epacket_dummy_set_tx_failure(0);
}

void *callback_setup(void)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	k_poll_signal_init(&tx_done_signal);
	k_poll_signal_init(&tx_fail_signal);
	epacket_register_callback(epacket_dummy, &interface_cb);

	return NULL;
}

ZTEST_SUITE(epacket_callbacks, NULL, callback_setup, NULL, NULL, NULL);
