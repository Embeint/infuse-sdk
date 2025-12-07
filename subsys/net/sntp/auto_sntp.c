/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket_service.h>

#include <infuse/net/dns.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#define SNTP_PORT 123

static void sntp_service_handler(struct net_socket_service_event *sev);

static struct kv_store_cb sntp_kv_cb;
static struct net_mgmt_event_callback l4_callback;
static struct k_work_delayable sntp_worker;
static struct k_work_delayable sntp_timeout;
static struct sockaddr sntp_addr_cached;
static socklen_t sntp_addrlen_cached;
static struct sntp_ctx sntp_context;
static uint8_t sntp_failures;

#ifdef CONFIG_SNTP_AUTO_IMMEDIATELY
static struct epoch_time_cb time_callback;
#endif /* CONFIG_SNTP_AUTO_IMMEDIATELY */

#ifdef CONFIG_SNTP_AUTO_SYNC_POINTS
static bool l4_connected;
#endif /* CONFIG_SNTP_AUTO_SYNC_POINTS */

NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(service_auto_sntp, sntp_service_handler, 1);

LOG_MODULE_REGISTER(sntp_auto, CONFIG_SNTP_AUTO_LOG_LEVEL);

static void sntp_error_handle(struct k_work_delayable *dwork)
{
	if (++sntp_failures < CONFIG_SNTP_AUTO_RETRY_LIMIT) {
		/* Failed to perform SNTP update, retry shortly */
		k_work_reschedule(dwork, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));
	} else {
		LOG_INF("Giving up SNTP queries after %d failures", CONFIG_SNTP_AUTO_RETRY_LIMIT);
		sntp_failures = 0;
		k_work_reschedule(dwork, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));
	}
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			     struct net_if *iface)
{
#ifdef CONFIG_SNTP_AUTO_SYNC_POINTS
	if (event == NET_EVENT_L4_CONNECTED) {
		l4_connected = true;
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		k_work_cancel_delayable(&sntp_worker);
		l4_connected = false;
	}
#endif /* CONFIG_SNTP_AUTO_SYNC_POINTS */
#ifdef CONFIG_SNTP_AUTO_IMMEDIATELY
	k_timeout_t delay = K_NO_WAIT;
	uint32_t sync_age;

	if (event == NET_EVENT_L4_CONNECTED) {
		sync_age = epoch_time_reference_age();
		if (sync_age < CONFIG_SNTP_AUTO_RESYNC_AGE) {
			delay = K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - sync_age);
		}
		sntp_failures = 0;
		k_work_reschedule(&sntp_worker, delay);
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		k_work_cancel_delayable(&sntp_worker);
	}
#endif /* CONFIG_SNTP_AUTO_IMMEDIATELY */
}

static void sntp_service_handler(struct net_socket_service_event *sev)
{
	struct sntp_time s_time;
	k_ticks_t ticks = k_uptime_ticks();
	int rc;

	/* Cancel timeout */
	k_work_cancel_delayable(&sntp_timeout);

#ifdef CONFIG_SNTP_AUTO_IMMEDIATELY
	/* Reschedule worker */
	sntp_failures = 0;
	k_work_reschedule(&sntp_worker, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));
#endif /* CONFIG_SNTP_AUTO_IMMEDIATELY */

	/* Read the response from the socket */
	rc = sntp_read_async(sev, &s_time);
	sntp_close_async(&service_auto_sntp);
	if (rc != 0) {
		LOG_WRN("Read failure");
		sntp_addrlen_cached = 0;
		return;
	}
	LOG_INF("Unix time: %llu", s_time.seconds);

	/* Update reference instant */
	struct timeutil_sync_instant sync_point = {
		.local = ticks,
		/* SNTP seconds fraction is [0, UINT32_MAX] */
		.ref = epoch_time_from_unix(s_time.seconds, s_time.fraction >> 16),
	};
	if (epoch_time_set_reference(TIME_SOURCE_NTP, &sync_point) < 0) {
		LOG_ERR("Failed to set reference (%d)", rc);
	}
}

static void sntp_timeout_work(struct k_work *work)
{
	LOG_WRN("SNTP query timeout");
	sntp_close_async(&service_auto_sntp);

	/* Re-query the DNS */
	sntp_addrlen_cached = 0;

	sntp_error_handle(&sntp_worker);
}

static int sntp_start_async_query(struct sockaddr *addr, socklen_t addrlen)
{
	int rc;

	rc = sntp_init_async(&sntp_context, addr, addrlen, &service_auto_sntp);
	if (rc < 0) {
		LOG_ERR("Failed to init ctx (%d)", rc);
		return rc;
	}

	LOG_INF("Sending request...");
	rc = sntp_send_async(&sntp_context);
	if (rc == 0) {
		k_work_schedule(&sntp_timeout, K_MSEC(CONFIG_SNTP_AUTO_QUERY_TIMEOUT_MS));
		return 0;
	}

	LOG_ERR("Failed to send request (%d)", rc);
	sntp_close_async(&service_auto_sntp);
	return rc;
}

#ifdef CONFIG_INFUSE_DNS_ASYNC

static void async_dns_cb(int result, struct sockaddr *addr, socklen_t addrlen,
			 struct infuse_async_dns_context *ctx)
{
	struct k_work_delayable *delayable = ctx->user_data;
	struct sockaddr_in *saddr_in;

	if (result < 0) {
		LOG_ERR("SNTP DNS query failed (%d)", result);
		goto error;
	}

	/* Next phase on first callback only */
	if ((ctx->user_data == NULL) || (result == INFUSE_ASYNC_DNS_COMPLETE)) {
		return;
	}
	ctx->user_data = NULL;

	/* Async DNS doesn't populate the port */
	BUILD_ASSERT(offsetof(struct sockaddr_in, sin_port) ==
		     offsetof(struct sockaddr_in6, sin6_port));
	saddr_in = (struct sockaddr_in *)addr;
	saddr_in->sin_port = htons(SNTP_PORT);

	/* Cache the address for future runs */
	memcpy(&sntp_addr_cached, addr, sizeof(*addr));
	sntp_addrlen_cached = addrlen;

	/* Start the async SNTP query */
	if (sntp_start_async_query(&sntp_addr_cached, sntp_addrlen_cached) < 0) {
		goto error;
	}

	return;
error:
	sntp_error_handle(delayable);
}

#endif /* CONFIG_INFUSE_DNS_ASYNC */

static void sntp_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	KV_STRING_CONST(ntp_default, CONFIG_SNTP_AUTO_DEFAULT_SERVER);
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;
	int rc;

	if (sntp_addrlen_cached > 0) {
		/* We still have a valid cached SNTP server address */
		LOG_INF("Using cached SNTP address");
		rc = sntp_start_async_query(&sntp_addr_cached, sntp_addrlen_cached);
		if (rc == 0) {
			return;
		}
		/* Failed to start the query */
		sntp_addrlen_cached = 0;
	}

	/* Pull NTP server address from KV store */
	rc = KV_STORE_READ_FALLBACK(KV_KEY_NTP_SERVER_URL, &ntp_server, &ntp_default);
	if (rc < 0) {
		/* Something very bad has happened, try to recover for next run */
		LOG_ERR("Failed to read NTP server url (%d)", rc);
		(void)kv_store_delete(KV_KEY_NTP_SERVER_URL);
		goto error;
	}

#ifdef CONFIG_INFUSE_DNS_ASYNC
	static struct infuse_async_dns_context sntp_dns_ctx = {
		.cb = async_dns_cb,
	};

	sntp_dns_ctx.user_data = delayable;

	rc = infuse_async_dns(ntp_server.url.value, AF_INET, &sntp_dns_ctx, 10000);
	if (rc < 0) {
		LOG_ERR("DNS failed to start query for %s (%d)", ntp_server.url.value, rc);
		goto error;
	}
	return;
#else
	/* Get IP address from DNS */
	rc = infuse_sync_dns(ntp_server.url.value, SNTP_PORT, AF_INET, SOCK_DGRAM,
			     &sntp_addr_cached, &sntp_addrlen_cached);
	if (rc < 0) {
		LOG_ERR("DNS query failed for %s (%d)", ntp_server.url.value, rc);
		sntp_addrlen_cached = 0;
		goto error;
	}

	/* Start the async SNTP query */
	rc = sntp_start_async_query(&sntp_addr_cached, sntp_addrlen_cached);
	if (rc == 0) {
		return;
	}

#endif /* CONFIG_INFUSE_DNS_ASYNC */

error:
	sntp_error_handle(delayable);
}

static void kv_value_changed(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	if (key == KV_KEY_NTP_SERVER_URL) {
		LOG_DBG("NTP server changed");
		/* Reset cache knowledge */
		sntp_addrlen_cached = 0;
	}
}

#ifdef CONFIG_SNTP_AUTO_SYNC_POINTS

void sntp_auto_sync_point(void)
{
	uint32_t sync_age;

	if (!l4_connected) {
		/* No network connectivity */
		return;
	}

	if (k_work_delayable_is_pending(&sntp_worker) ||
	    k_work_delayable_is_pending(&sntp_timeout)) {
		/* SNTP query already running */
		LOG_DBG("SNTP query already running");
		return;
	}

	sync_age = epoch_time_reference_age();
	if (sync_age < CONFIG_SNTP_AUTO_RESYNC_AGE) {
		/* SNTP not yet required */
		return;
	}

	/* Schedule the SNTP query */
	sntp_failures = 0;
	k_work_reschedule(&sntp_worker, K_NO_WAIT);
}

#endif /* CONFIG_SNTP_AUTO_SYNC_POINTS */

#ifdef CONFIG_SNTP_AUTO_IMMEDIATELY

static void reference_time_updated(enum epoch_time_source source, struct timeutil_sync_instant old,
				   struct timeutil_sync_instant new, void *user_ctx)
{
	struct k_work_delayable *worker = user_ctx;

	ARG_UNUSED(source);
	ARG_UNUSED(old);
	ARG_UNUSED(new);

	/* Reschedule time sync */
	if (k_work_delayable_is_pending(worker)) {
		k_work_reschedule(worker, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));
	}
}

#endif /* CONFIG_SNTP_AUTO_IMMEDIATELY */

int sntp_auto_init(void)
{
	k_work_init_delayable(&sntp_worker, sntp_work);
	k_work_init_delayable(&sntp_timeout, sntp_timeout_work);

	/* Register for callbacks on KV changes */
	sntp_kv_cb.value_changed = kv_value_changed;
	kv_store_register_callback(&sntp_kv_cb);

#ifdef CONFIG_SNTP_AUTO_IMMEDIATELY
	/* Register for callbacks on time updates */
	time_callback.reference_time_updated = reference_time_updated;
	time_callback.user_ctx = &sntp_worker;
	epoch_time_register_callback(&time_callback);
#endif /* CONFIG_SNTP_AUTO_IMMEDIATELY */

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&l4_callback, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_callback);

	return 0;
}

SYS_INIT(sntp_auto_init, POST_KERNEL, 0);
