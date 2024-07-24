/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <time.h>

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/socket.h>

#include <infuse/net/coap.h>
#include <infuse/net/dns.h>

K_SEM_DEFINE(l4_up, 0, 1);

/* Static COAP test server run by TZI */
const char *coap_test_server = "coap.me";
uint8_t work_area[2048];

struct cb_ctx {
	const char *expected_data;
	uint32_t expected_offset;
	uint32_t cb_count;
};

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
			     struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

void data_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
{
	struct cb_ctx *ctx = context;

	zassert_equal(ctx->expected_offset, offset);
	if (ctx->expected_data) {
		zassert_mem_equal(ctx->expected_data + offset, data, data_len);
	}
	ctx->expected_offset += data_len;
	ctx->cb_count += 1;
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

ZTEST(infuse_coap, test_bad_params)
{
	int sock = 0, rc;

	/* No data callback handler */
	rc = infuse_coap_download(sock, "test", NULL, NULL, work_area, sizeof(work_area), 1000);
	zassert_equal(-EINVAL, rc);

	/* Path with too many components */
	rc = infuse_coap_download(sock, "a/b/c/d/e/f/g/h/i/g", data_cb, NULL, work_area,
				  sizeof(work_area), 1000);
	zassert_equal(-EINVAL, rc);

	/* Path that is way too long for a 128 byte work area */
	const char *long_uri =
		"this_path_is_way_too_long_and_should_trigger_the_append_resource_path_error "
		"this_path_is_way_too_long_and_should_trigger_the_append_resource_path_error";

	rc = infuse_coap_download(sock, long_uri, data_cb, NULL, work_area, 128, 1000);
	zassert_equal(-EINVAL, rc);
}

ZTEST(infuse_coap, test_invalid_work_area)
{
	struct cb_ctx context;
	int sock = 0, rc;

	rc = infuse_coap_download(sock, "hello", data_cb, &context, work_area, 32, 1000);
	zassert_equal(-ENOMEM, rc);
}

ZTEST(infuse_coap, test_bad_socket)
{
	struct cb_ctx context;
	int sock = 0, rc;

	/* Download from bad socket */
	rc = infuse_coap_download(sock, "hello", data_cb, &context, work_area, sizeof(work_area),
				  1000);
	zassert_equal(-EBADF, rc);
}

ZTEST(infuse_coap, test_resource_errors)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

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

ZTEST(infuse_coap, test_timeout)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

	/* Open socket */
	sock = socket_setup();

	/* Request a packet with a timeout that can't be met */
	context.expected_data = "world";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "hello", data_cb, &context, work_area, sizeof(work_area),
				  10);
	zassert_equal(-ETIMEDOUT, rc);

	/* Request another resource, "world" response should be discarded due to token mismatch */
	context.expected_data = "You asked me about: Nothing particular.";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "query", data_cb, &context, work_area, sizeof(work_area),
				  1000);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

static int socket_to_close;

void async_socket_close(struct k_work *work)
{
	(void)zsock_close(socket_to_close);
}

ZTEST(infuse_coap, test_socket_close)
{
	struct k_work_delayable work;
	struct cb_ctx context;
	int sock;
	int rc;

	k_work_init_delayable(&work, async_socket_close);

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

	/* Open socket */
	sock = socket_setup();

	/* Schedule socket to be closed in 25ms */
	socket_to_close = sock;
	k_work_reschedule(&work, K_MSEC(25));

	/* Send a request with the knowledge the socket will close */
	context.expected_data = "world";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "hello", data_cb, &context, work_area, sizeof(work_area),
				  1000);
	zassert_equal(-EBADF, rc);

	k_sem_give(&l4_up);
}

ZTEST(infuse_coap, test_download)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

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
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

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
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

	return NULL;
}

ZTEST_SUITE(infuse_coap, NULL, test_init, NULL, NULL, NULL);
