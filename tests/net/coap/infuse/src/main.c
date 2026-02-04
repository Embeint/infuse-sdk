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
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/socket.h>

#include <infuse/identifiers.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/net/coap.h>
#include <infuse/net/dns.h>
#include <infuse/security.h>

#ifdef CONFIG_WIFI
#define IF_DELAY K_SECONDS(20)
#else
#define IF_DELAY K_SECONDS(5)
#endif

K_SEM_DEFINE(l4_up, 0, 1);

const char *const coap_test_server = "coap.dev.infuse-iot.com";

/* Hardcoded authentication that only has access to test files */
const uint8_t test_identity[8] = {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t test_psk[32] = {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
uint8_t work_area[4096];

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

static int data_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
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
	const sec_tag_t sec_tls_tags[] = {
		infuse_security_coap_dtls_tag(),
	};
	struct sockaddr address;
	socklen_t address_len;
	int sock;
	int rc;

	/* IPv4 lookup */
	zassert_equal(0, infuse_sync_dns(coap_test_server, 5684, AF_INET, SOCK_DGRAM, &address,
					 &address_len));

	/* Create socket */
	sock = zsock_socket(address.sa_family, SOCK_DGRAM, IPPROTO_DTLS_1_2);
	zassert_true(sock >= 0);

	/* Assign DTLS security tags */
	zassert_equal(0, zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tls_tags,
					  sizeof(sec_tls_tags)));

	/* Complete DTLS handshake */
	rc = zsock_connect(sock, &address, address_len);
	if (rc != 0) {
		zsock_close(sock);
	}
	zassert_equal(0, rc);
	return sock;
}

ZTEST(infuse_coap, test_bad_params)
{
	int sock = 0, rc;

	/* No data callback handler */
	rc = infuse_coap_download(sock, "test", 0, NULL, NULL, work_area, sizeof(work_area), 0,
				  1000);
	zassert_equal(-EINVAL, rc);

	/* Path with too many components */
	rc = infuse_coap_download(sock, "a/b/c/d/e/f/g/h/i/g", 0, data_cb, NULL, work_area,
				  sizeof(work_area), 0, 1000);
	zassert_equal(-EINVAL, rc);

	/* Path that is way too long for a 128 byte work area */
	const char *long_uri =
		"this_path_is_way_too_long_and_should_trigger_the_append_resource_path_error "
		"this_path_is_way_too_long_and_should_trigger_the_append_resource_path_error";

	rc = infuse_coap_download(sock, long_uri, 0, data_cb, NULL, work_area, 128, 0, 1000);
	zassert_equal(-EINVAL, rc);
}

ZTEST(infuse_coap, test_invalid_work_area)
{
	struct cb_ctx context;
	int sock = 0, rc;

	rc = infuse_coap_download(sock, "file/small_file", 0, data_cb, &context, work_area, 32, 0,
				  1000);
	zassert_equal(-ENOMEM, rc);
}

ZTEST(infuse_coap, test_bad_socket)
{
	struct cb_ctx context;
	int sock = -1, rc;

	/* Download from bad socket */
	rc = infuse_coap_download(sock, "file/small_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 0, 1000);
	zassert_equal(-EBADF, rc);
}

ZTEST(infuse_coap, test_timeout)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* Request a packet with a timeout that can't be met */
	context.expected_data = NULL;
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "file/med_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 0, 1);
	zassert_equal(-ETIMEDOUT, rc);

	/* Request another resource, "world" response should be discarded due to token mismatch */
	context.expected_data = "hello_world\n";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "file/small_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 0, 1000);

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
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* Schedule socket to be closed in 10ms */
	socket_to_close = sock;
	k_work_reschedule(&work, K_MSEC(10));

	/* Send a request with the knowledge the socket will close */
	context.expected_data = "hello_world\n";
	context.expected_offset = 0;
	rc = infuse_coap_download(sock, "file/small_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 0, 1000);
	zassert_equal(-EBADF, rc);

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

	/* Short retrieval: file/small_file -> hello_world\n */
	context.expected_data = "hello_world\n";
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "file/small_file", 0, data_cb, &context, work_area, 128, 0,
				  1000);
	zassert_equal(strlen(context.expected_data), rc);
	zassert_equal(1, context.cb_count);

	/* Medium retrieval: file/med_file -> 10030 bytes == 10 packets at 1024 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "file/med_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 1024, 1000);
	zassert_equal(10030, rc);
	zassert_equal(10, context.cb_count);

	/* Medium retrieval with known size: file/med_file */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "file/med_file", 10030, data_cb, &context, work_area,
				  sizeof(work_area), 1024, 1000);
	zassert_equal(10030, rc);
	zassert_equal(10, context.cb_count);

	/* Medium retrieval: file/med_file -> 10030 bytes == 20 packets at 512 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "file/med_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 512, 1000);
	zassert_equal(10030, rc);
	zassert_equal(20, context.cb_count);

	/* Medium retrieval: file/med_file -> 10030 bytes == 40 packets at 256 block size */
	context.expected_data = NULL;
	context.expected_offset = 0;
	context.cb_count = 0;
	rc = infuse_coap_download(sock, "file/med_file", 0, data_cb, &context, work_area,
				  sizeof(work_area), 256, 1000);
	zassert_equal(10030, rc);
	zassert_equal(40, context.cb_count);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

static int early_term_cb(uint32_t offset, const uint8_t *data, uint16_t data_len, void *context)
{
	return -ECHILD;
}

ZTEST(infuse_coap, test_download_early_terminate)
{
	struct cb_ctx context;
	int sock;
	int rc;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Open socket */
	sock = socket_setup();

	/* Attempt to download, error should match that from callback */
	rc = infuse_coap_download(sock, "file/med_file", 0, early_term_cb, &context, work_area,
				  sizeof(work_area), 0, 1000);
	zassert_equal(-ECHILD, rc);

	/* Close socket */
	zassert_equal(0, zsock_close(sock));
	k_sem_give(&l4_up);
}

void *test_init(void)
{
	static struct net_mgmt_event_callback mgmt_cb;

#ifndef CONFIG_INFUSE_COMMON_BOOT
	infuse_security_init();
#endif

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
