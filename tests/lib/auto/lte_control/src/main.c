/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include <infuse/auto/lte_control.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/lib/lte_modem_monitor.h>
#include <infuse/states.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/tdf.h>

K_SEM_DEFINE(disconnect_started, 0, 1);
K_SEM_DEFINE(disconnect_continue, 0, 1);

static struct {
	int connect_calls;
	int disconnect_calls;
	int connect_rc;
	int disconnect_rc;
	struct net_if *last_connect_iface;
	struct net_if *last_disconnect_iface;
	bool registered;
	bool block_disconnect;
} test_ctx;

static struct net_if *expected_lte_iface(void)
{
	return net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
}

int conn_mgr_if_connect(struct net_if *iface)
{
	test_ctx.connect_calls++;
	test_ctx.last_connect_iface = iface;
	return test_ctx.connect_rc;
}

int conn_mgr_if_disconnect(struct net_if *iface)
{
	test_ctx.disconnect_calls++;
	test_ctx.last_disconnect_iface = iface;
	if (test_ctx.block_disconnect) {
		k_sem_give(&disconnect_started);
		k_sem_take(&disconnect_continue, K_FOREVER);
	}
	return test_ctx.disconnect_rc;
}

bool lte_modem_monitor_is_registered(void)
{
	return test_ctx.registered;
}

static void wait_for_calls(int connect_calls, int disconnect_calls)
{
	for (int i = 0; i < 50; i++) {
		if ((test_ctx.connect_calls == connect_calls) &&
		    (test_ctx.disconnect_calls == disconnect_calls)) {
			return;
		}
		k_sleep(K_MSEC(1));
	}
	zassert_equal(connect_calls, test_ctx.connect_calls);
	zassert_equal(disconnect_calls, test_ctx.disconnect_calls);
}

static void expect_no_more_calls(void)
{
	int connect_calls = test_ctx.connect_calls;
	int disconnect_calls = test_ctx.disconnect_calls;

	k_sleep(K_MSEC(20));
	zassert_equal(connect_calls, test_ctx.connect_calls);
	zassert_equal(disconnect_calls, test_ctx.disconnect_calls);
}

static void drain_tdf_logs(void)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	while ((pkt = k_fifo_get(tx_queue, K_NO_WAIT)) != NULL) {
		net_buf_unref(pkt);
	}
}

static void expect_lte_control_log(uint8_t expected_enabled)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_lte_control *control;
	struct tdf_parsed tdf;
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	zassert_not_null(pkt);

	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));
	zassert_equal(0, tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_LTE_CONTROL, &tdf));
	zassert_equal(1, tdf.tdf_num);
	control = tdf.data;
	zassert_equal(expected_enabled, control->enabled);
	zassert_is_null(k_fifo_get(tx_queue, K_MSEC(1)));

	net_buf_unref(pkt);
}

static void expect_no_lte_control_log(void)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	zassert_is_null(k_fifo_get(tx_queue, K_MSEC(1)));
}

ZTEST(lte_control, test_inactive_on_startup_does_not_connect)
{
	auto_lte_control_init(0);
	expect_no_more_calls();
}

ZTEST(lte_control, test_active_on_startup_connects)
{
	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	auto_lte_control_init(0);
	wait_for_calls(1, 0);
	zassert_equal(expected_lte_iface(), test_ctx.last_connect_iface);
}

ZTEST(lte_control, test_application_active_state_controls_lte)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);
	zassert_equal(expected_lte_iface(), test_ctx.last_connect_iface);

	infuse_state_clear(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 1);
	zassert_equal(expected_lte_iface(), test_ctx.last_disconnect_iface);
}

ZTEST(lte_control, test_already_active_does_not_reconnect)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	expect_no_more_calls();
}

ZTEST(lte_control, test_give_up_disconnects_only_when_connect_requested_and_unregistered)
{
	auto_lte_control_init(0);

	auto_lte_control_give_up();
	expect_no_more_calls();

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);

	test_ctx.registered = true;
	auto_lte_control_give_up();
	expect_no_more_calls();

	test_ctx.registered = false;
	auto_lte_control_give_up();
	wait_for_calls(1, 1);

	auto_lte_control_give_up();
	expect_no_more_calls();
}

ZTEST(lte_control, test_retry_connects_only_when_active_after_disconnect)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);
	auto_lte_control_give_up();
	wait_for_calls(1, 1);

	infuse_state_clear(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 2);
	auto_lte_control_retry();
	expect_no_more_calls();

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(2, 2);
	auto_lte_control_give_up();
	wait_for_calls(2, 3);

	auto_lte_control_retry();
	wait_for_calls(3, 3);
}

ZTEST(lte_control, test_retry_skips_when_not_previously_disconnected)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);

	auto_lte_control_retry();
	expect_no_more_calls();
}

ZTEST(lte_control, test_retry_connects_when_give_up_disconnect_is_pending)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);

	test_ctx.block_disconnect = true;
	auto_lte_control_give_up();
	zassert_ok(k_sem_take(&disconnect_started, K_MSEC(100)));

	auto_lte_control_retry();
	k_sem_give(&disconnect_continue);
	wait_for_calls(2, 1);
}

ZTEST(lte_control, test_failed_connect_does_not_enable_give_up)
{
	auto_lte_control_init(0);
	test_ctx.connect_rc = -EIO;

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);

	auto_lte_control_give_up();
	expect_no_more_calls();
}

ZTEST(lte_control, test_tdf_logging_connect_disconnect)
{
	auto_lte_control_init(TDF_DATA_LOGGER_SERIAL);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);
	expect_lte_control_log(1);

	infuse_state_clear(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 1);
	expect_lte_control_log(0);
}

ZTEST(lte_control, test_tdf_logging_uses_configured_loggers)
{
	auto_lte_control_init(0);

	infuse_state_set(INFUSE_STATE_APPLICATION_ACTIVE);
	wait_for_calls(1, 0);
	expect_no_lte_control_log();
}

static void reset_counters(void)
{
	test_ctx.connect_calls = 0;
	test_ctx.disconnect_calls = 0;
	test_ctx.last_connect_iface = NULL;
	test_ctx.last_disconnect_iface = NULL;
}

static void reset_lte_control_state(void *fixture)
{
	ARG_UNUSED(fixture);

	auto_lte_control_test_cleanup();
	if (infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE)) {
		infuse_state_clear(INFUSE_STATE_APPLICATION_ACTIVE);
	}
	test_ctx.connect_rc = 0;
	test_ctx.disconnect_rc = 0;
	test_ctx.registered = false;
	test_ctx.block_disconnect = false;
	k_sem_reset(&disconnect_started);
	k_sem_reset(&disconnect_continue);
	drain_tdf_logs();
	reset_counters();
}

static void cleanup_lte_control_state(void *fixture)
{
	ARG_UNUSED(fixture);

	auto_lte_control_test_cleanup();
	if (infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE)) {
		infuse_state_clear(INFUSE_STATE_APPLICATION_ACTIVE);
	}
}

ZTEST_SUITE(lte_control, NULL, NULL, reset_lte_control_state, cleanup_lte_control_state, NULL);
