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

#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/net/auto_sntp.h>

#ifdef CONFIG_WIFI
#define IF_DELAY K_SECONDS(20)
#else
#define IF_DELAY K_SECONDS(5)
#endif

K_SEM_DEFINE(l4_up, 0, 1);
K_SEM_DEFINE(time_ref_updated, 0, 5);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	/* Our manual invalid time point */
	if (source == TIME_SOURCE_GNSS) {
		return;
	}

	zassert_equal(TIME_SOURCE_NTP, source, "Unexpected time source");
	zassert_equal(&time_ref_updated, user_ctx, "Mismatched user context");

#ifdef CONFIG_NATIVE_LIBC
	uint64_t civil = epoch_time_now();
	time_t from_libc = time(NULL);
	time_t from_sntp = unix_time_from_epoch(civil);

	/* Ensure SNTP time roughly matches local system_time */
	printk("Local Time: %d\n", from_libc);
	printk(" SNTP Time: %d\n", from_sntp);
	zassert_within(from_libc, from_sntp, 2);
#endif /* CONFIG_NATIVE_LIBC */

	k_sem_give(&time_ref_updated);
}

ZTEST(auto_sntp, test_auto_sntp_triggered)
{
	static struct epoch_time_cb time_cb;

	/* Remove any pending URLs */
	(void)kv_store_delete(KV_KEY_NTP_SERVER_URL);

	/* Register for time callbacks */
	time_cb.reference_time_updated = reference_time_updated;
	time_cb.user_ctx = &time_ref_updated;
	epoch_time_register_callback(&time_cb);

	/* Trigger before interface is up */
	sntp_auto_sync_point();

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

	/* Wait for the interface to come up */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY));

	/* By default, nothing happens */
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_SECONDS(2)));
	zassert_false(kv_store_key_exists(KV_KEY_NTP_SERVER_URL));

	/* Trigger the SNTP sync point (twice to check only triggered once) */
	sntp_auto_sync_point();
	sntp_auto_sync_point();

	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));

	/* Check that the NTP server URL was written */
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;

	zassert_true(KV_STORE_READ(KV_KEY_NTP_SERVER_URL, &ntp_server) > 0);

	/* Trigger again too soon, nothing happens */
	sntp_auto_sync_point();

	zassert_equal(-EAGAIN,
		      k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE + 1)));

	/* Resync time has passed, should query again */
	sntp_auto_sync_point();
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));
	k_sleep(K_SECONDS(1));
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

	return NULL;
}

ZTEST_SUITE(auto_sntp, NULL, test_init, NULL, NULL, NULL);
