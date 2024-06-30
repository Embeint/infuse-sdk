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

ZTEST(epacket_common, test_alloc_failure)
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

ZTEST(epacket_common, test_receive)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	/* Working as expected */
	epacket_dummy_receive_api_override(true, 0);

	/* No work scheduled, requested to stop */
	zassert_false(epacket_dummy_receive_scheduled());
	zassert_equal(1, epacket_receive(epacket_dummy, K_NO_WAIT));
	zassert_false(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(1));

	/* No work scheduled, request for 1 second */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request for 2 seconds then 1 second */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(2)));
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	k_sleep(K_MSEC(950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request for 1 second then 2 seconds */
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(1)));
	zassert_equal(1, epacket_receive(epacket_dummy, K_SECONDS(2)));
	k_sleep(K_MSEC(1950));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(100));
	zassert_false(epacket_dummy_receive_scheduled());

	/* No work scheduled, request forever */
	zassert_equal(0, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_true(epacket_dummy_receive_scheduled());
	k_sleep(K_MSEC(2100));
	zassert_true(epacket_dummy_receive_scheduled());
	/* Cancel immediately */
	zassert_equal(1, epacket_receive(epacket_dummy, K_NO_WAIT));
	k_sleep(K_MSEC(1));
	zassert_false(epacket_dummy_receive_scheduled());
}

ZTEST(epacket_common, test_receive_no_impl)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_dummy_receive_api_override(false, 0);

	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_NO_WAIT));
	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_equal(-ENOTSUP, epacket_receive(epacket_dummy, K_SECONDS(2)));

	epacket_dummy_receive_api_override(true, 0);
}

ZTEST(epacket_common, test_receive_error)
{
	const struct device *epacket_dummy = DEVICE_DT_GET(DT_NODELABEL(epacket_dummy));

	epacket_dummy_receive_api_override(true, -EIO);
	zassert_false(epacket_dummy_receive_scheduled());

	/* Function call should not have requested the enable */
	zassert_equal(1, epacket_receive(epacket_dummy, K_NO_WAIT));
	/* Should fail to enable */
	zassert_equal(-EIO, epacket_receive(epacket_dummy, K_FOREVER));
	zassert_false(epacket_dummy_receive_scheduled());
	zassert_equal(-EIO, epacket_receive(epacket_dummy, K_SECONDS(2)));
	zassert_false(epacket_dummy_receive_scheduled());

	epacket_dummy_receive_api_override(true, 0);
}

ZTEST_SUITE(epacket_common, NULL, NULL, NULL, NULL, NULL);
