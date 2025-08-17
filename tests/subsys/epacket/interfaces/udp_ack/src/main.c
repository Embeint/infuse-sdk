/**
 * @file
 * @copyright 2024 Embeint Inc
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
static int tdf_buffers_recovered;
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
	struct epacket_tx_metadata *tx_meta = net_buf_user_data(buf);
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;

	/* Sends with socket closed don't have a payload */
	if (buf->len > 0) {
		zassert_equal(INFUSE_TDF, tx_meta->type);

		/* We expect all failures to give us back a decrypted packet we can parse.
		 * We know this test only sends a single TDF_AMBIENT_TEMPERATURE
		 */
		tdf_parse_start(&state, buf->data, buf->len);
		zassert_equal(0, tdf_parse(&state, &parsed));
		zassert_equal(TDF_AMBIENT_TEMPERATURE, parsed.tdf_id);
		zassert_equal(sizeof(struct tdf_ambient_temperature), parsed.tdf_len);
		zassert_equal(1, parsed.tdf_num);
		zassert_equal(-ENOMEM, tdf_parse(&state, &parsed));

		tdf_buffers_recovered += 1;
	}

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

ZTEST(epacket_udp_ack, test_send_before_conn)
{
	/* Send a packet not requesting an ACK */
	tdf_send(0, epacket_tx_done);
	zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(1000)));
	zassert_equal(-ENOTCONN, tx_done_result);

	/* Send a packet requesting an ACK */
	tdf_send(EPACKET_FLAGS_ACK_REQUEST, epacket_tx_done);
	zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(1000)));
	zassert_equal(-ENOTCONN, tx_done_result);
}

ZTEST(epacket_udp_ack, test_no_ack)
{
	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));

	/* Push packets without ACK requests */
	for (int i = 0; i < (CONFIG_EPACKET_BUFFERS_TX + 1); i++) {
		tdf_send(0, NULL);
	}
	k_sleep(K_SECONDS(1));
}

ZTEST(epacket_udp_ack, test_udp_ack_handling)
{
	struct net_buf *rx;

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));

	/* Send a packet requesting an ACK */
	tdf_send(EPACKET_FLAGS_ACK_REQUEST, epacket_tx_done);

	/* Callback should not be run immediately, should wait for the ACK to come in */
	zassert_equal(-EAGAIN, k_sem_take(&tx_done_sem, K_MSEC(10)));

	/* Callback should not be run immediately, should wait for the ACK to come in */
	zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(1000)));
	zassert_equal(0, tx_done_result);

	/* ACK should still be pushed to the queue */
	rx = k_fifo_get(&udp_rx_fifo, K_MSEC(1));
	zassert_not_null(rx);
}

ZTEST(epacket_udp_ack, test_udp_ack_timeout)
{
	struct kv_epacket_udp_port udp_port = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT};
	uint32_t timeout_ms = CONFIG_EPACKET_INTERFACE_UDP_DETECT_UNACKNOWLEDGED_TIMEOUT_MS;

	tdf_buffers_recovered = 0;

	/* Set incorrect UDP port, which will cause ACKs to timeout */
	udp_port.port -= 1;
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port, sizeof(udp_port));

	/* Turn on the interface */
	conn_mgr_all_if_up(true);
	zassert_equal(0, k_sem_take(&if_state_change, K_MSEC(100)));

	/* Correct the port for the next time its queried */
	udp_port.port += 1;
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port, sizeof(udp_port));

	/* Send packets that won't be ACKed one by one */
	for (int i = 0; i < 4; i++) {
		/* Send a packet requesting an ACK */
		tdf_send(EPACKET_FLAGS_ACK_REQUEST, epacket_tx_done);
		/* Callback should not be run immediately, should wait for the ACK to come in */
		zassert_equal(-EAGAIN, k_sem_take(&tx_done_sem, K_MSEC(timeout_ms - 50)));
		/* Callback should timeout */
		zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(100)));
		zassert_equal(-ENODATA, tx_done_result);
	}

	/* Send 2 packets */
	tdf_send(EPACKET_FLAGS_ACK_REQUEST, epacket_tx_done);
	k_sleep(K_MSEC(500));
	tdf_send(EPACKET_FLAGS_ACK_REQUEST, epacket_tx_done);

	/* First request should timeout relative to first send, not second */
	zassert_equal(-EAGAIN, k_sem_take(&tx_done_sem, K_MSEC(timeout_ms - 550)));
	zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(100)));
	zassert_equal(-ENODATA, tx_done_result);

	/* Next request should timeout 500ms after the first */
	zassert_equal(-EAGAIN, k_sem_take(&tx_done_sem, K_MSEC(450)));
	zassert_equal(0, k_sem_take(&tx_done_sem, K_MSEC(100)));
	zassert_equal(-ENODATA, tx_done_result);

	/* Ensure we recovered all 6 TDF buffers */
	zassert_equal(6, tdf_buffers_recovered);
}

static struct epacket_interface_cb udp_if_cb = {
	.interface_state = udp_interface_state,
	.tx_failure = udp_tx_failure,
};

static void test_init(void *state)
{
	KV_STRING_CONST(udp_url_default, CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_URL);
	struct kv_epacket_udp_port udp_port_default = {CONFIG_EPACKET_INTERFACE_UDP_DEFAULT_PORT};

	/* Write default configuration to KV store */
	kv_store_write(KV_KEY_EPACKET_UDP_PORT, &udp_port_default, sizeof(udp_port_default));
	kv_store_write(KV_KEY_EPACKET_UDP_URL, &udp_url_default, sizeof(udp_url_default));

	k_sem_reset(&tx_done_sem);
	k_sem_reset(&if_state_change);
	k_sem_reset(&if_tx_failure);
	k_sem_reset(&downlink_watchdog_expired);

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

ZTEST_SUITE(epacket_udp_ack, NULL, testsuite_init, test_init, test_after, NULL);
