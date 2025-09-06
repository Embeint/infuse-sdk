/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <time.h>

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/socket.h>

#include <infuse/net/coap.h>
#include <infuse/net/dns.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#ifdef CONFIG_WIFI
#define IF_DELAY K_SECONDS(20)
#else
#define IF_DELAY K_SECONDS(5)
#endif

K_SEM_DEFINE(l4_up, 0, 1);

/* Static COAP test server run by TZI */
const char *const coap_test_server = "coap.me";
uint8_t work_area[2048];

struct cb_ctx {
	const char *expected_data;
	uint32_t expected_offset;
	uint32_t cb_count;
};

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

int data_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
{
	struct cb_ctx *ctx = context;

	zassert_equal(ctx->expected_offset, offset);
	if (ctx->expected_data) {
		zassert_mem_equal(ctx->expected_data + offset, data, data_len);
	}
	ctx->expected_offset += data_len;
	ctx->cb_count += 1;

	return 0;
}

static int socket_setup(void)
{
	struct sockaddr address;
	socklen_t address_len;
	int sock;

	/* IPv4 lookup */
	zassert_equal(0, infuse_sync_dns(coap_test_server, 5683, AF_INET, SOCK_STREAM, &address,
					 &address_len));

	/* Create socket */
	sock = zsock_socket(address.sa_family, SOCK_DGRAM, IPPROTO_UDP);
	zassert_true(sock >= 0);
	zassert_equal(0, zsock_connect(sock, &address, address_len));

	return sock;
}

ZTEST(infuse_coap, test_resource_errors)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* 401 response: secret */
	context.expected_data = NULL;
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "secret", data_cb, &context, work_area, sizeof(work_area),
				  1000);
	zassert_equal(-401, rc);

	/* 404 response: invalid-path */
	context.expected_data = NULL;
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "invalid-path", data_cb, &context, work_area,
				  sizeof(work_area), 1000);
	zassert_equal(-404, rc);

	/* 405 response: location-query */
	context.expected_data = NULL;
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "location-query", data_cb, &context, work_area,
				  sizeof(work_area), 1000);
	zassert_equal(-405, rc);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

ZTEST(infuse_coap, test_download)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* Short retrieval: hello -> world */
	context.expected_data = "world";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "hello", data_cb, &context, work_area, 128, 1000);
	zassert_equal(strlen(context.expected_data), rc);

	/* Multi component URI: seg1/seg2/seg3 -> Matroshka */
	context.expected_data = "Matroshka";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "seg1/seg2/seg3", data_cb, &context, work_area, 200, 1000);
	zassert_equal(strlen(context.expected_data), rc);

	/* Long retrieval: large -> 1700 bytes == 2 packets at 1024 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "large", data_cb, &context, work_area, sizeof(work_area),
				  1000);
	zassert_equal(1700, rc);
	zassert_equal(2, context.cb_count);

	/* Long retrieval: large -> 1700 bytes == 4 packets at 512 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "large", data_cb, &context, work_area, 700, 1000);
	zassert_equal(1700, rc);
	zassert_equal(4, context.cb_count);

	/* Long retrieval: large -> 1700 bytes == 4 packets at 256 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "large", data_cb, &context, work_area, 400, 1000);
	zassert_equal(1700, rc);
	zassert_equal(7, context.cb_count);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

ZTEST(infuse_coap, test_separate_response)
{
	uint8_t work_area[256];
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* Server responds with ACK immediately, then data after ~5 seconds */
	context.expected_data = "That took a long time";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "separate", data_cb, &context, work_area, sizeof(work_area),
				  7000);
	zassert_equal(strlen(context.expected_data), rc);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

void *test_init(void)
{
	static struct net_mgmt_event_callback mgmt_cb;

#ifdef CONFIG_WIFI
	KV_STRING_CONST(ssid, CONFIG_WIFI_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_PSK);

	kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid));
	kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk));
#endif /* CONFIG_WIFI */

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb, l4_event_handler, NET_EVENT_L4_CONNECTED);
		net_mgmt_add_event_callback(&mgmt_cb);
	}

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	struct net_if *iface = net_if_get_default();
	struct in_addr addr;

	/* Add the IP address to trigger NET_EVENT_L4_CONNECTED */
	net_addr_pton(AF_INET, "192.0.2.1", &addr);
	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
#else
	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

	return NULL;
}

ZTEST_SUITE(infuse_coap, NULL, test_init, NULL, NULL, NULL);
