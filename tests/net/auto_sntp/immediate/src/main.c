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

#ifdef CONFIG_WIFI
#define IF_DELAY K_SECONDS(20)
#else
#define IF_DELAY K_SECONDS(5)
#endif

K_SEM_DEFINE(l4_up, 0, 1);
K_SEM_DEFINE(time_ref_updated, 0, 1);

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
static struct net_if *iface;
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

static void l4_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
			     size_t info_length, void *user_data)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_up);
	}
}

/* Statically registered so that connectivity gained during boot is not missed */
NET_MGMT_REGISTER_EVENT_HANDLER(l4_events, NET_EVENT_L4_CONNECTED, l4_event_handler, NULL);

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

ZTEST(auto_sntp, test_auto_sntp)
{
	KV_STRING_CONST(sntp_valid, CONFIG_SNTP_AUTO_DEFAULT_SERVER);
	KV_STRING_CONST(sntp_invalid, "www.google.com");

	struct timeutil_sync_instant reference;

	/* Wait for time to be updated */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));

	/* Check that the NTP server URL was written */
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;

	zassert_true(KV_STORE_READ(KV_KEY_NTP_SERVER_URL, &ntp_server) > 0);

	/* Change the SNTP server address to an invalid value, which should timeout twice in a row,
	 * with a DNS requery in between
	 */
	int timeout_ms = 2 * ((CONFIG_SNTP_AUTO_RESYNC_AGE * MSEC_PER_SEC) +
			      CONFIG_SNTP_AUTO_QUERY_TIMEOUT_MS) +
			 500;

	zassert_true(KV_STORE_WRITE(KV_KEY_NTP_SERVER_URL, &sntp_invalid) > 0);
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_MSEC(timeout_ms)));

	/* Reset back to a valid server */
	zassert_true(KV_STORE_WRITE(KV_KEY_NTP_SERVER_URL, &sntp_valid) > 0);

	/* Wait for the next sync */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE + 1)));

	/* Wait a while, manually set the reference to reset the resync age */
	reference.local = k_uptime_ticks();
	reference.ref = 10000000;
	zassert_equal(-EAGAIN,
		      k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1)));
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));

	/* Ensure the time sync was delayed by the previous reference */
	zassert_equal(-EAGAIN,
		      k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1)));
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(2)));

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	/* Take the interface down to trigger NET_EVENT_L4_DISCONNECTED */
	zassert_equal(0, net_if_down(iface));

	/* No more callbacks */
	zassert_equal(-EAGAIN,
		      k_sem_take(&time_ref_updated, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE + 1)));

	/* Set time sync while disconnected */
	k_sleep(K_MSEC(500));
	reference.local = k_uptime_ticks();
	zassert_equal(0, epoch_time_set_reference(TIME_SOURCE_GNSS, &reference));
	k_sleep(K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - 1));

	/* Reconnect by bringing the interface up */
	zassert_equal(0, net_if_up(iface));

	/* Time sync shouldn't trigger immediately */
	zassert_equal(-EAGAIN, k_sem_take(&time_ref_updated, K_MSEC(500)));

	/* But should occur at the expected time */
	zassert_equal(0, k_sem_take(&time_ref_updated, K_SECONDS(1)));
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */
}

void *test_init(void)
{
	static struct epoch_time_cb time_cb;

#ifdef CONFIG_WIFI
	KV_STRING_CONST(ssid, CONFIG_WIFI_SSID);
	KV_STRING_CONST(psk, CONFIG_WIFI_PSK);

	kv_store_write(KV_KEY_WIFI_SSID, &ssid, sizeof(ssid));
	kv_store_write(KV_KEY_WIFI_PSK, &psk, sizeof(psk));
#endif /* CONFIG_WIFI */

	/* Remove any pending URLs */
	(void)kv_store_delete(KV_KEY_NTP_SERVER_URL);

	/* Register for time callbacks */
	time_cb.reference_time_updated = reference_time_updated;
	time_cb.user_ctx = &time_ref_updated;
	epoch_time_register_callback(&time_cb);

#ifdef CONFIG_NET_NATIVE_OFFLOADED_SOCKETS
	iface = net_if_get_default();

	/* Bring the interface up to trigger NET_EVENT_L4_CONNECTED */
	net_if_up(iface);
#else
	/* Turn on all interfaces */
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);
#endif /* CONFIG_NET_NATIVE_OFFLOADED_SOCKETS */

	/* Wait for the interface to come up before running any tests */
	zassert_equal(0, k_sem_take(&l4_up, IF_DELAY), "Timed out waiting for L4 connectivity");

	return NULL;
}

ZTEST_SUITE(auto_sntp, NULL, test_init, NULL, NULL, NULL);
