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

	zassert_not_equal(0, infuse_sync_dns("not.a.real.address", 80, AF_INET6, SOCK_STREAM,
					     &address, &address_len));
#endif /* defined(CONFIG_NET_IPV6) && !defined(CONFIG_WIFI) */
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

ZTEST_SUITE(infuse_dns, NULL, test_init, NULL, NULL, NULL);
