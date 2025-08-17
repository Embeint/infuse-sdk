/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>

#include <infuse/reboot.h>
#include <infuse/security.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_udp.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/tdf/tdf.h>
#include <infuse/tdf/definitions.h>

#include "../subsys/epacket/interfaces/epacket_internal.h"

#define IF_UDP DEVICE_DT_GET_ONE(embeint_epacket_udp)

K_FIFO_DEFINE(udp_rx_fifo);
K_SEM_DEFINE(tx_done_sem, 0, 1);
static int tx_done_result;

K_SEM_DEFINE(if_state_change, 0, 1);
static int if_max_payload;
K_SEM_DEFINE(if_tx_failure, 0, 1);
static int if_tx_failure_reason;
K_SEM_DEFINE(downlink_watchdog_expired, 0, 1);
static enum infuse_reboot_reason reboot_reason;

void infuse_reboot(enum infuse_reboot_reason reason, uint32_t info1, uint32_t info2)
{
	reboot_reason = reason;
	k_sem_give(&downlink_watchdog_expired);
}

static void rx_fifo_pusher(struct net_buf *buf)
{
	k_fifo_put(&udp_rx_fifo, buf);
}

void udp_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	if_max_payload = current_max_payload;
	k_sem_give(&if_state_change);
}

void udp_tx_failure(const struct net_buf *buf, int reason, void *user_ctx)
{
	if_tx_failure_reason = reason;
	k_sem_give(&if_tx_failure);
}

static void epacket_tx_done(const struct device *dev, struct net_buf *pkt, int result,
			    void *user_data)
{
	tx_done_result = result;
	k_sem_give(&tx_done_sem);
}

static void tdf_send(uint16_t flags, epacket_tx_done_cb tx_cb)
{
	struct tdf_ambient_temperature temperature;
	struct tdf_buffer_state tdf_state;
	struct net_buf *tx;

	tx = epacket_alloc_tx_for_interface(IF_UDP, K_MSEC(100));
	zassert_not_null(tx);

	/* Send a random TDF that requests an acknowledgment */
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, flags, INFUSE_TDF, EPACKET_ADDR_ALL);
	if (tx_cb != NULL) {
		epacket_set_tx_callback(tx, tx_cb, NULL);
	}
	tdf_state.time = 0;
	tdf_state.buf = tx->b;
	temperature.temperature = 25000;
	tdf_add(&tdf_state, TDF_AMBIENT_TEMPERATURE, sizeof(temperature), 1, 0, 0, &temperature);
	tx->b = tdf_state.buf;
	epacket_queue(IF_UDP, tx);
}

static void test_acked_packet(void)
{
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *rx;

	/* Send a packet requesting an ACK */
	tdf_send(EPACKET_FLAGS_ACK_REQUEST, NULL);

	/* Expect an ACK response */
	rx = k_fifo_get(&udp_rx_fifo, K_MSEC(1000));
	zassert_not_null(rx);
	rx_meta = net_buf_user_data(rx);
	zassert_equal(INFUSE_ACK, rx_meta->type);
	net_buf_unref(rx);
}

ZTEST(epacket_udp, test_udp_send_before_conn)
{
	for (int i = 0; i < CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT + 2; i++) {
		tdf_send(0, epacket_tx_done);
		zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(100)));
		zassert_equal(-ENOTCONN, tx_done_result);
		zassert_equal(0, k_sem_take(&if_tx_failure, K_MSEC(100)));
		zassert_equal(-ENOTCONN, if_tx_failure_reason);
	}

	/* Watchdog should not have expired since application never requested connectivity */
	zassert_equal(-EBUSY, k_sem_take(&downlink_watchdog_expired, K_NO_WAIT));
}

ZTEST(epacket_udp, test_udp_auto_ack)
{
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *rx;

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));

	/* Send packets until automated ACK is expected */
	for (int i = 0; i < (CONFIG_EPACKET_INTERFACE_UDP_ACK_PERIOD_SEC + 1); i++) {
		tdf_send(0, NULL);
		k_sleep(K_SECONDS(1));
	}

	/* Expected an ACK packet to be generated, which should have resulted in a response */
	rx = k_fifo_get(&udp_rx_fifo, K_MSEC(100));
	zassert_not_null(rx);
	rx_meta = net_buf_user_data(rx);
	zassert_equal(INFUSE_ACK, rx_meta->type);
	net_buf_unref(rx);
}

ZTEST(epacket_udp, test_udp_ack)
{
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *rx;

	/* Cycle the interface a few times before testing */
	for (int i = 0; i < 4; i++) {
		conn_mgr_all_if_up(true);
		zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(10)));
		k_sleep(K_MSEC(10));
		conn_mgr_all_if_down(false);
		zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(10)));
		/* Interface has a 1 second cooling off period */
		k_sleep(K_MSEC(1010));
	}
	zassert_equal(0, if_max_payload);

	/* Turn on the interface */
	conn_mgr_all_if_up(true);

	/* Expect the callback */
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));
	zassert_true(if_max_payload > 0);

	for (int i = 0; i < 3; i++) {
		/* Send a packet requesting an ACK */
		tdf_send(EPACKET_FLAGS_ACK_REQUEST, NULL);
		zassert_equal(-EAGAIN, k_sem_take(&if_tx_failure, K_MSEC(10)));

		/* Expect an ACK response */
		rx = k_fifo_get(&udp_rx_fifo, K_MSEC(1000));
		zassert_not_null(rx);
		rx_meta = net_buf_user_data(rx);
		zassert_equal(INFUSE_ACK, rx_meta->type);
		net_buf_unref(rx);

		k_sleep(K_MSEC(500));
	}

	/* Expect no more packets */
	rx = k_fifo_get(&udp_rx_fifo, K_MSEC(1000));
	zassert_is_null(rx);

	uint32_t wdog_initial = CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG_TIMEOUT - 2;

	/* Does not expire until period after last ack */
	zassert_equal(-EAGAIN, k_sem_take(&downlink_watchdog_expired, K_SECONDS(wdog_initial)));
	zassert_equal(0, k_sem_take(&downlink_watchdog_expired, K_SECONDS(2)));

	/* Turn off the interface */
	conn_mgr_all_if_down(false);

	/* Expect the callback */
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));
	zassert_equal(0, if_max_payload);
}

ZTEST(epacket_udp, test_udp_max_size)
{
	struct epacket_rx_metadata *rx_meta;
	struct net_buf *rx;
	struct net_buf *tx;

	/* Turn on the interface */
	conn_mgr_all_if_up(true);

	/* Expect the callback */
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));
	zassert_true(if_max_payload > 0);

	tx = epacket_alloc_tx_for_interface(IF_UDP, K_MSEC(100));
	zassert_not_null(tx);

	/* Send a random packet that requests an acknowledgment */
	epacket_set_tx_metadata(tx, EPACKET_AUTH_DEVICE, EPACKET_FLAGS_ACK_REQUEST, 0xFF,
				EPACKET_ADDR_ALL);
	net_buf_add(tx, net_buf_tailroom(tx));
	epacket_queue(IF_UDP, tx);

	/* Expect an ACK response */
	rx = k_fifo_get(&udp_rx_fifo, K_MSEC(1000));
	zassert_not_null(rx);
	rx_meta = net_buf_user_data(rx);
	zassert_equal(INFUSE_ACK, rx_meta->type);
	net_buf_unref(rx);

	/* Turn off the interface */
	conn_mgr_all_if_down(false);

	/* Expect the callback */
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));
	zassert_equal(0, if_max_payload);
}

ZTEST(epacket_udp, test_udp_reconnect)
{
	KV_KEY_TYPE(KV_KEY_EPACKET_UDP_PORT)
	udp_port_default = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT - 1};
	struct net_buf *rx;
	int rc;

	/* Set incorrect UDP port */
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port_default, sizeof(udp_port_default));

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));

	/* Correct the port for the next time its queried */
	udp_port_default.port += 1;
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port_default, sizeof(udp_port_default));

	/* Send packets until we expect the connection to be dropped */
	for (int i = 0; i < (CONFIG_EPACKET_INTERFACE_UDP_ACK_PERIOD_SEC +
			     CONFIG_EPACKET_INTERFACE_UDP_ACK_COUNTDOWN + 1);
	     i++) {
		tdf_send(0, NULL);
		rc = k_sem_take(&if_state_change, K_SECONDS(1));
		if (rc == 0) {
			/* Interface is now disconnected */
			break;
		}
	}
	/* Ensure loop exited due to disconnect */
	zassert_equal(0, rc);

	/* No packets expected up until disconnect */
	rx = k_fifo_get(&udp_rx_fifo, K_NO_WAIT);
	zassert_is_null(rx);

	/* We expect the interface to go up again */
	zassert_equal(0, k_sem_take(&if_state_change, K_SECONDS(2)));

	/* Interface should still work */
	test_acked_packet();
}

ZTEST(epacket_udp, test_udp_bad_dns)
{
	KV_STRING_CONST(udp_url_default, CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_URL);
	KV_STRING_CONST(udp_url_bad, "udp2.dev.infuse-iot.com");

	kv_store_write(KV_KEY_EPACKET_UDP_URL, &udp_url_bad, sizeof(udp_url_bad));

	/* Turn on the interface */
	conn_mgr_all_if_up(true);

	/* Interface should not report ready */
	zassert_equal(-EAGAIN, k_sem_take(&if_state_change, K_MSEC(3500)));

	/* Fix the URL */
	kv_store_write(KV_KEY_EPACKET_UDP_URL, &udp_url_default, sizeof(udp_url_default));

	/* Connection should be good */
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(1500)));

	/* Interface should work */
	test_acked_packet();
}

static struct epacket_interface_cb udp_if_cb = {
	.interface_state = udp_interface_state,
	.tx_failure = udp_tx_failure,
};

static void test_init(void *state)
{
	KV_STRING_CONST(udp_url_default, CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_URL);
	KV_KEY_TYPE(KV_KEY_EPACKET_UDP_PORT)
	udp_port_default = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT};

	/* Write default configuration to KV store */
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port_default, sizeof(udp_port_default));
	kv_store_write(KV_KEY_EPACKET_UDP_URL, &udp_url_default, sizeof(udp_url_default));

	k_sem_take(&tx_done_sem, K_NO_WAIT);
	k_sem_take(&if_state_change, K_NO_WAIT);
	k_sem_take(&if_tx_failure, K_NO_WAIT);
	k_sem_take(&downlink_watchdog_expired, K_NO_WAIT);

	epacket_set_receive_handler(IF_UDP, rx_fifo_pusher);
}

static void test_after(void *fixture)
{
	struct net_buf *rx;

	conn_mgr_all_if_disconnect(false);
	conn_mgr_all_if_down(false);
	k_sleep(K_MSEC(1010));
	epacket_udp_dns_reset();

	do {
		rx = k_fifo_get(&udp_rx_fifo, K_MSEC(100));
		if (rx) {
			net_buf_unref(rx);
		}
	} while (rx);
}

static void *testsuite_init(void)
{
	struct net_if *iface = net_if_get_default();
	struct in_addr addr;

	conn_mgr_all_if_down(false);
	epacket_register_callback(IF_UDP, &udp_if_cb);
	infuse_security_init();

	/* Add the IP address to trigger NET_EVENT_L4_CONNECTED */
	net_addr_pton(AF_INET, "192.0.2.1", &addr);
	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);

	return NULL;
}

ZTEST_SUITE(epacket_udp, NULL, testsuite_init, test_init, test_after, NULL);
