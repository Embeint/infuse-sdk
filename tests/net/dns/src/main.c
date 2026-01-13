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

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/net/dns.h>

#ifdef CONFIG_WIFI
#define IF_DELAY K_SECONDS(20)
#else
#define IF_DELAY K_SECONDS(5)
#endif

K_SEM_DEFINE(l4_up, 0, 1);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

ZTEST(infuse_dns, test_dns_query)
{
	struct sockaddr address;
	socklen_t address_len;

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* IPv4 lookups */
	zassert_equal(
		0, infuse_sync_dns("google.com", 80, AF_INET, SOCK_STREAM, &address, &address_len));
	zassert_equal(sizeof(struct sockaddr_in), address_len);

	zassert_not_equal(0, infuse_sync_dns("not.a.real.address", 80, AF_INET, SOCK_STREAM,
					     &address, &address_len));

	/* Not working on WiFi network, not sure how to validate at runtime if this should work */
#if defined(CONFIG_NET_IPV6) && !defined(CONFIG_WIFI)
	/* IPv6 lookups */
	zassert_equal(0, infuse_sync_dns("google.com", 80, AF_INET6, SOCK_STREAM, &address,
					 &address_len));
	zassert_equal(sizeof(struct sockaddr_in6), address_len);

#ifndef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	/* IPv6 queries through getaddrinfo to invalid addresses on POSIX can take a long time and
	 * break the other tests for currently unknown reasons.
	 */
	zassert_not_equal(0, infuse_sync_dns("not.a.real.address", 80, AF_INET6, SOCK_STREAM,
					     &address, &address_len));
#endif /* !CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */
#endif /* defined(CONFIG_NET_IPV6) && !defined(CONFIG_WIFI) */

	/* Interface is still up */
	k_sem_give(&l4_up);
}

#ifdef CONFIG_INFUSE_DNS_ASYNC

static K_SEM_DEFINE(async_success, 0, 1);
static K_SEM_DEFINE(async_complete, 0, 1);
static K_SEM_DEFINE(async_failure, 0, 1);

static void async_dns_cb(int result, struct sockaddr *addr, socklen_t addrlen,
			 struct infuse_async_dns_context *cb_ctx)
{
	socklen_t *address_len;

	zassert_not_null(cb_ctx);

	address_len = cb_ctx->user_data;
	if (result == INFUSE_ASYNC_DNS_RESULT) {
		/* Completion event has not occurred (Previous results may have been provided) */
		zassert_equal(-EBUSY, k_sem_take(&async_complete, K_NO_WAIT));
		/* Address is provided */
		zassert_not_null(addr);
		zassert_not_equal(0, addrlen);
		/* Store/notify result */
		*address_len = addrlen;
		k_sem_give(&async_success);
	} else if (result == INFUSE_ASYNC_DNS_COMPLETE) {
		zassert_is_null(addr);
		zassert_equal(0, addrlen);
		k_sem_give(&async_complete);
	} else {
		zassert_is_null(addr);
		zassert_equal(0, addrlen);
		k_sem_give(&async_failure);
	}
}

ZTEST(infuse_dns, test_dns_query_async)
{
	socklen_t address_len;
	struct infuse_async_dns_context cb_ctx = {
		.cb = async_dns_cb,
		.user_data = &address_len,
	};

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* Invalid address families */
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_UNSPEC, &cb_ctx, 2000));
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_PACKET, &cb_ctx, 2000));
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_CAN, &cb_ctx, 2000));
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_NET_MGMT, &cb_ctx, 2000));
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_LOCAL, &cb_ctx, 2000));
	zassert_equal(-EINVAL, infuse_async_dns("google.com", AF_UNIX, &cb_ctx, 2000));

	/* IPv4 lookups */
	zassert_equal(0, infuse_async_dns("google.com", AF_INET, &cb_ctx, 2000));
	zassert_equal(0, k_sem_take(&async_success, K_SECONDS(2)));
	zassert_equal(0, k_sem_take(&async_complete, K_MSEC(100)));
	zassert_equal(-EBUSY, k_sem_take(&async_failure, K_NO_WAIT));
	zassert_equal(sizeof(struct sockaddr_in), address_len);

	zassert_equal(0, infuse_async_dns("not.a.real.address", AF_INET, &cb_ctx, 2000));
	zassert_equal(0, k_sem_take(&async_failure, K_SECONDS(2)));
	zassert_equal(-EBUSY, k_sem_take(&async_success, K_NO_WAIT));
	zassert_equal(-EBUSY, k_sem_take(&async_complete, K_NO_WAIT));

	/* Not working on WiFi network, not sure how to validate at runtime if this should work */
#if defined(CONFIG_NET_IPV6) && !defined(CONFIG_WIFI)
	/* IPv6 lookups */
	zassert_equal(0, infuse_async_dns("google.com", AF_INET6, &cb_ctx, 2000));
	zassert_equal(0, k_sem_take(&async_success, K_SECONDS(2)));
	zassert_equal(0, k_sem_take(&async_complete, K_MSEC(100)));
	zassert_equal(-EBUSY, k_sem_take(&async_failure, K_NO_WAIT));
	zassert_equal(sizeof(struct sockaddr_in6), address_len);

	zassert_equal(0, infuse_async_dns("not.a.real.address", AF_INET6, &cb_ctx, 2000));
	zassert_equal(0, k_sem_take(&async_failure, K_SECONDS(2)));
	zassert_equal(-EBUSY, k_sem_take(&async_success, K_NO_WAIT));
	zassert_equal(-EBUSY, k_sem_take(&async_complete, K_NO_WAIT));
#endif /* defined(CONFIG_NET_IPV6) && !defined(CONFIG_WIFI) */

	/* Interface is still up */
	k_sem_give(&l4_up);
}

#else

ZTEST(infuse_dns, test_dns_query_async)
{
	ztest_test_skip();
}

#endif /* CONFIG_INFUSE_DNS_ASYNC */

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

ZTEST_SUITE(infuse_dns, NULL, test_init, NULL, NULL, NULL);
