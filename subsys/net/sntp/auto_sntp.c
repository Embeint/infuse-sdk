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

static void sntp_service_handler(struct net_socket_service_event *sev);

static struct epoch_time_cb time_callback;
static struct net_mgmt_event_callback l4_callback;
static struct k_work_delayable sntp_worker;
static struct k_work_delayable sntp_timeout;
static struct sntp_ctx sntp_context;

NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(service_auto_sntp, sntp_service_handler, 1);

LOG_MODULE_REGISTER(sntp_auto, CONFIG_SNTP_AUTO_LOG_LEVEL);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			     struct net_if *iface)
{
	k_timeout_t delay = K_NO_WAIT;
	uint32_t sync_age;

	if (event == NET_EVENT_L4_CONNECTED) {
		sync_age = epoch_time_reference_age();
		if (sync_age < CONFIG_SNTP_AUTO_RESYNC_AGE) {
			delay = K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - sync_age);
		}
		k_work_reschedule(&sntp_worker, delay);
	}
	if (event == NET_EVENT_L4_DISCONNECTED) {
		k_work_cancel_delayable(&sntp_worker);
	}
}

static void sntp_service_handler(struct net_socket_service_event *sev)
{
	struct sntp_time s_time;
	k_ticks_t ticks = k_uptime_ticks();
	int rc;

	/* Cancel timeout */
	k_work_cancel_delayable(&sntp_timeout);

	/* Reschedule worker */
	k_work_reschedule(&sntp_worker, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));

	/* Read the response from the socket */
	rc = sntp_read_async(sev, &s_time);
	sntp_close_async(&service_auto_sntp);
	if (rc != 0) {
		LOG_WRN("Read failure");
		return;
	}
	LOG_INF("Unix time: %llu", s_time.seconds);

	/* Update reference instant */
	struct timeutil_sync_instant sync_point = {
		.local = ticks,
		.ref = epoch_time_from_unix(s_time.seconds, s_time.fraction / 15259),
	};
	if (epoch_time_set_reference(TIME_SOURCE_NTP, &sync_point) < 0) {
		LOG_ERR("Failed to set reference (%d)", rc);
	}
}

static void sntp_timeout_work(struct k_work *work)
{
	LOG_WRN("SNTP query timeout");
	sntp_close_async(&service_auto_sntp);

	/* Reschedule worker */
	k_work_reschedule(&sntp_worker, K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE));
}

static void sntp_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	KV_STRING_CONST(ntp_default, CONFIG_SNTP_AUTO_DEFAULT_SERVER);
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;
	struct sockaddr addr;
	socklen_t addrlen;
	int rc;

	/* Pull NTP server address from KV store */
	rc = KV_STORE_READ_FALLBACK(KV_KEY_NTP_SERVER_URL, &ntp_server, &ntp_default);
	if (rc < 0) {
		/* Something very bad has happened, try to recover for next run */
		LOG_ERR("Failed to read NTP server url (%d)", rc);
		(void)kv_store_delete(KV_KEY_NTP_SERVER_URL);
		goto error;
	}

	/* Get IP address from DNS */
	rc = infuse_sync_dns(ntp_server.url.value, 123, AF_INET, SOCK_DGRAM, &addr, &addrlen);
	if (rc < 0) {
		LOG_ERR("DNS query failed for %s (%d)", ntp_server.url.value, rc);
		goto error;
	}

	rc = sntp_init_async(&service_auto_sntp, &sntp_context, &addr, addrlen);
	if (rc < 0) {
		LOG_ERR("Failed to init ctx (%d)", rc);
		goto error;
	}

	LOG_INF("Sending request...");
	rc = sntp_send_async(&sntp_context);
	if (rc == 0) {
		k_work_schedule(&sntp_timeout, K_MSEC(CONFIG_SNTP_QUERY_TIMEOUT_MS));
		return;
	}

	LOG_ERR("Failed to send request (%d)", rc);
	sntp_close_async(&service_auto_sntp);

error:
	/* Failed to perform SNTP update, retry in 5 seconds */
	k_work_reschedule(delayable, K_SECONDS(5));
}

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

int sntp_auto_init(void)
{
	k_work_init_delayable(&sntp_worker, sntp_work);
	k_work_init_delayable(&sntp_timeout, sntp_timeout_work);

	/* Register for callbacks on time updates */
	time_callback.reference_time_updated = reference_time_updated;
	time_callback.user_ctx = &sntp_worker;
	epoch_time_register_callback(&time_callback);

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&l4_callback, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_callback);

	return 0;
}

SYS_INIT(sntp_auto_init, POST_KERNEL, 0);
