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

#include <infuse/net/dns.h>

K_SEM_DEFINE(l4_up, 0, 1);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
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

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	struct net_if *iface = net_if_get_default();
	struct in_addr addr;

	/* Add the IP address to trigger NET_EVENT_L4_CONNECTED */
	zassert_equal(0, net_addr_pton(AF_INET, "192.0.2.1", &addr));
	zassert_not_null(net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0));
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

	/* IPv4 lookups */
	zassert_equal(
		0, infuse_sync_dns("google.com", 80, AF_INET, SOCK_STREAM, &address, &address_len));
	zassert_equal(sizeof(struct sockaddr_in), address_len);

	zassert_not_equal(0, infuse_sync_dns("not.a.real.address", 80, AF_INET, SOCK_STREAM,
					     &address, &address_len));

	/* IPv6 lookups */
	zassert_equal(0, infuse_sync_dns("google.com", 80, AF_INET6, SOCK_STREAM, &address,
					 &address_len));
	zassert_equal(sizeof(struct sockaddr_in6), address_len);

	zassert_not_equal(0, infuse_sync_dns("not.a.real.address", 80, AF_INET6, SOCK_STREAM,
					     &address, &address_len));
}

void *test_init(void)
{
	static struct net_mgmt_event_callback mgmt_cb;

	if (IS_ENABLED(CONFIG_NET_CONNECTION_MANAGER)) {
		net_mgmt_init_event_callback(&mgmt_cb, l4_event_handler, NET_EVENT_L4_CONNECTED);
		net_mgmt_add_event_callback(&mgmt_cb);
	}
	return NULL;
}

ZTEST_SUITE(infuse_dns, NULL, test_init, NULL, NULL, NULL);
