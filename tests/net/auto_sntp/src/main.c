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

#include <infuse/time/civil.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

K_SEM_DEFINE(l4_up, 0, 1);
K_SEM_DEFINE(time_ref_updated, 0, 1);

static int kv_init(void)
{
	return kv_store_init();
}

SYS_INIT(kv_init, POST_KERNEL, 60);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

static void reference_time_updated(enum civil_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	/* Our manual invalid time point */
	if (source == TIME_SOURCE_INVALID) {
		return;
	}

	zassert_equal(TIME_SOURCE_NTP, source, "Unexpected time source");
	zassert_equal(&time_ref_updated, user_ctx, "Mismatched user context");

#ifdef CONFIG_NATIVE_LIBC
	uint64_t civil = civil_time_now();
	time_t from_libc = time(NULL);
	time_t from_sntp = unix_time_from_civil(civil);

	/* Ensure SNTP time roughly matches local system_time */
	printk("Local Time: %d\n", from_libc);
	printk(" SNTP Time: %d\n", from_sntp);
	zassert_within(from_libc, from_sntp, 2);
#endif /* CONFIG_NATIVE_LIBC */

	k_sem_give(&time_ref_updated);
}

ZTEST(auto_sntp, test_boot)
{
	struct timeutil_sync_instant reference;
	static struct civil_time_cb time_cb;
	struct net_if *iface = net_if_get_default();

	/* Register for time callbacks */
	time_cb.reference_time_updated = reference_time_updated;
	time_cb.user_ctx = &time_ref_updated;
	civil_time_register_callback(&time_cb);

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	struct in_addr addr;

	/* Add the IP address to trigger NET_EVENT_L4_CONNECTED */
	zassert_equal(0, net_addr_pton(AF_INET, "192.0.2.1", &addr));
	zassert_not_null(net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0));
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, K_SECONDS(5)));

	/* Wait for time to be updated */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));

	/* Check that the NTP server URL was written */
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;

	zassert_true(KV_STORE_READ(KV_KEY_NTP_SERVER_URL, &ntp_server) > 0);

	/* Wait for the next sync */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE + 1)));

	/* Wait a while, manually set the reference to reset the resync age */
	reference.local = k_uptime_ticks();
	reference.ref = 10000000;
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1)));
	zassert_equal(0, civil_time_set_reference(TIME_SOURCE_INVALID, &reference));

	/* Ensure the time sync was delayed by the previous reference */
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1)));
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	/* Remove the IP address to trigger NET_EVENT_L4_DISCONNECTED */
	zassert_true(net_if_ipv4_addr_rm(iface, &addr));

	/* No more callbacks */
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE + 1)));

	/* Set time sync while disconnected */
	k_sleep(K_MSEC(500));
	zassert_equal(0, civil_time_set_reference(TIME_SOURCE_INVALID, &reference));
	k_sleep(K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1));

	/* Reconnect by adding IP address */
	zassert_not_null(net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0));

	/* Ensure time sync happends quickly */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */
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

ZTEST_SUITE(auto_sntp, NULL, test_init, NULL, NULL, NULL);
