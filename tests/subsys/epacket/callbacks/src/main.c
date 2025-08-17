/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>

static struct epacket_interface_cb interface_cb;
static struct k_poll_signal tx_fail_signal;
static struct k_poll_signal tx_done_signal;
static struct k_poll_signal rx_recv_signal;

static void tx_failed_cb(const struct net_buf *buf, int reason, void *user_ctx)
{
	zassert_not_null(buf);
	zassert_equal(user_ctx, &interface_cb);

	k_poll_signal_raise(&tx_fail_signal, reason);
}

static bool packet_received_cb(struct net_buf *buf, bool decrypted, void *user_ctx)
{
	zassert_not_null(buf);
	zassert_equal(user_ctx, &interface_cb);

	k_poll_signal_raise(&rx_recv_signal, (int)decrypted);

	return true;
}

static void *expected_user_data;

static void tx_done(const struct device *dev, struct net_buf *buf, int result, void *user_data)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	zassert_equal(epacket_dummy, dev);
	zassert_equal(expected_user_data, user_data);
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
	zassert_is_null(k_fifo_get(sent_queue, K_NO_WAIT));

	interface_cb.tx_failure = tx_failed_cb;
	interface_cb.packet_received = packet_received_cb;
	interface_cb.user_ctx = &interface_cb;

	epacket_register_callback(epacket_dummy, &interface_cb);
	epacket_set_receive_handler(epacket_dummy, epacket_default_receive_handler);

	/* Allocate buffer */
	tx = epacket_alloc_tx_for_interface(epacket_dummy, K_NO_WAIT);
	zassert_not_null(tx);
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20, EPACKET_ADDR_ALL);
	expected_user_data = NULL;
	epacket_set_tx_callback(tx, tx_done, NULL);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it was sent properly */
	sent = k_fifo_get(sent_queue, K_MSEC(1));
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
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, 0x1234, 0x20, EPACKET_ADDR_ALL);
	expected_user_data = payload;
	epacket_set_tx_callback(tx, tx_done, payload);
	net_buf_add_mem(tx, payload, sizeof(payload));

	/* Send buffer on interface */
	epacket_queue(epacket_dummy, tx);

	/* Validate it wasn't sent properly, callback received */
	zassert_is_null(k_fifo_get(sent_queue, K_MSEC(1)));
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

	/* Receive a packet on the interface */
	k_poll_signal_check(&rx_recv_signal, &signaled, &result);
	zassert_equal(0, signaled);

	struct epacket_dummy_frame frame = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x00,
	};

	epacket_dummy_receive(epacket_dummy, &frame, payload, sizeof(payload));
	k_sleep(K_MSEC(1));
	k_poll_signal_check(&rx_recv_signal, &signaled, &result);
	zassert_equal(1, signaled);
	zassert_equal(1, result);
	k_poll_signal_reset(&rx_recv_signal);

	/* Unregister from callback */
	zassert_true(epacket_unregister_callback(epacket_dummy, &interface_cb));
	zassert_false(epacket_unregister_callback(epacket_dummy, &interface_cb));

	/* Callbacks should no longer run */
	epacket_dummy_receive(epacket_dummy, &frame, payload, sizeof(payload));
	k_sleep(K_MSEC(1));
	k_poll_signal_check(&rx_recv_signal, &signaled, &result);
	zassert_equal(0, signaled);
}

static bool packet_received_block_cb(struct net_buf *buf, bool decrypted, void *user_ctx)
{
	zassert_not_null(buf);
	zassert_equal(user_ctx, &interface_cb);

	k_poll_signal_raise(&rx_recv_signal, (int)decrypted);

	/* We don't want the default handler run */
	return false;
}

static void interface_unreachable_handler(struct net_buf *buf)
{
	/* Default interface handler should never run */
	zassert_unreachable();
}

ZTEST(epacket_callbacks, test_interface_rx_stop_default)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	uint8_t payload[16];
	int signaled, result;

	zassert_not_null(sent_queue);
	zassert_is_null(k_fifo_get(sent_queue, K_NO_WAIT));

	interface_cb.tx_failure = NULL;
	interface_cb.packet_received = packet_received_block_cb;
	interface_cb.user_ctx = &interface_cb;

	epacket_register_callback(epacket_dummy, &interface_cb);
	epacket_set_receive_handler(epacket_dummy, interface_unreachable_handler);

	struct epacket_dummy_frame frame = {
		.type = INFUSE_RPC_CMD,
		.auth = EPACKET_AUTH_DEVICE,
		.flags = 0x00,
	};

	for (int i = 0; i < 10; i++) {
		epacket_dummy_receive(epacket_dummy, &frame, payload, sizeof(payload));
		k_sleep(K_MSEC(1));
		k_poll_signal_check(&rx_recv_signal, &signaled, &result);
		zassert_equal(1, signaled);
		zassert_equal(1, result);
		k_poll_signal_reset(&rx_recv_signal);
	}
	zassert_true(epacket_unregister_callback(epacket_dummy, &interface_cb));
	k_sleep(K_MSEC(1));
}

void *callback_setup(void)
{
	k_poll_signal_init(&tx_done_signal);
	k_poll_signal_init(&tx_fail_signal);
	k_poll_signal_init(&rx_recv_signal);
	return NULL;
}

ZTEST_SUITE(epacket_callbacks, NULL, callback_setup, NULL, NULL, NULL);
