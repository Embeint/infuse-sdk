/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>

#include <infuse/time/civil.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

static struct civil_time_cb time_callback;
static struct net_mgmt_event_callback l4_callback;
static struct k_work_delayable sntp_worker;

LOG_MODULE_REGISTER(sntp_auto, CONFIG_SNTP_AUTO_LOG_LEVEL);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event, struct net_if *iface)
{
	k_timeout_t delay = K_NO_WAIT;
	uint32_t sync_age;

	if (event == NET_EVENT_L4_CONNECTED) {
		sync_age = civil_time_reference_age();
		if (sync_age < CONFIG_SNTP_AUTO_RESYNC_AGE) {
			delay = K_SECONDS(CONFIG_SNTP_AUTO_RESYNC_AGE - sync_age);
		}
		k_work_reschedule(&sntp_worker, delay);
	}
	if (event == NET_EVENT_L4_DISCONNECTED) {
		k_work_cancel_delayable(&sntp_worker);
	}
}

static void sntp_work(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM,
	};
	KV_STRING_CONST(ntp_default, CONFIG_SNTP_AUTO_DEFAULT_SERVER);
	KV_KEY_TYPE_VAR(KV_KEY_NTP_SERVER_URL, 64) ntp_server;
	struct zsock_addrinfo *res = NULL;
	struct sockaddr_in addr = {0};
	struct sntp_time sntp_time;
	struct sntp_ctx ctx;
	uint8_t *a;
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
	rc = zsock_getaddrinfo(ntp_server.url.value, NULL, &hints, &res);
	if (rc < 0) {
		LOG_ERR("DNS query failed for %s (%d)", ntp_server.url.value, rc);
		goto error;
	}
	addr = *(struct sockaddr_in *)res->ai_addr;
	zsock_freeaddrinfo(res);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(123);

	a = addr.sin_addr.s4_addr;
	LOG_INF("%s -> %d.%d.%d.%d:%d", ntp_server.url.value, a[0], a[1], a[2], a[3], ntohs(addr.sin_port));

	rc = sntp_init(&ctx, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (rc < 0) {
		LOG_ERR("Failed to init ctx (%d)", rc);
		goto error;
	}

	LOG_INF("Sending request...");
	rc = sntp_query(&ctx, 4 * MSEC_PER_SEC, &sntp_time);
	sntp_close(&ctx);
	if (rc < 0) {
		LOG_ERR("Request failed (%d)", rc);
		goto error;
	}
	LOG_INF("Unix time: %llu", sntp_time.seconds);

	/* Update reference instant */
	struct timeutil_sync_instant sync_point = {
		.local = k_uptime_ticks(),
		.ref = civil_time_from_unix(sntp_time.seconds, sntp_time.fraction / 15259),
	};
	if (civil_time_set_reference(TIME_SOURCE_NTP, &sync_point) < 0) {
		LOG_ERR("Failed to set reference (%d)", rc);
		goto error;
	}
	return;

error:
	/* Failed to perform SNTP update, retry in 5 seconds */
	k_work_reschedule(delayable, K_SECONDS(5));
}

static void reference_time_updated(enum civil_time_source source, struct timeutil_sync_instant old,
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

	/* Register for callbacks on time updates */
	time_callback.reference_time_updated = reference_time_updated;
	time_callback.user_ctx = &sntp_worker;
	civil_time_register_callback(&time_callback);

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&l4_callback, l4_event_handler, NET_EVENT_L4_CONNECTED);
	net_mgmt_add_event_callback(&l4_callback);

	return 0;
}

SYS_INIT(sntp_auto_init, POST_KERNEL, 0);
