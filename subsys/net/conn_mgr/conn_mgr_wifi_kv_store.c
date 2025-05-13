/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct k_work_delayable conn_config_changed;
static struct k_work_delayable conn_create;
static struct k_work_delayable conn_timeout;
static struct k_work conn_terminate;
static struct net_if *wifi_if;

LOG_MODULE_REGISTER(wifi_mgmt, LOG_LEVEL_INF);

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT

#include <supp_events.h>

static struct net_mgmt_event_callback wpa_supp_cb;
static bool wpa_ready;

static void wpa_supp_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				   struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_SUPPLICANT_READY:
		LOG_DBG("WPA_SUPP_READY");
		wpa_ready = true;
		break;
	case NET_EVENT_SUPPLICANT_NOT_READY:
		LOG_DBG("WPA_SUPP_NOT_READY");
		wpa_ready = false;
		break;
	default:
		break;
	}
}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT */

static void conn_create_worker(struct k_work *work)
{
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_SSID, WIFI_SSID_MAX_LEN) wifi_ssid;
	KV_KEY_TYPE_VAR(KV_KEY_WIFI_PSK, WIFI_PSK_MAX_LEN) wifi_psk;
	struct wifi_connect_req_params params = {0};

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT
	if (!wpa_ready) {
		struct k_work_delayable *delayable = k_work_delayable_from_work(work);

		/* WPA supplicant needs a few milliseconds to initialise after IF up */
		k_work_reschedule(delayable, K_MSEC(5));
		LOG_DBG("Delaying for WPA supplicant");
		return;
	}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT */

	/* Load connection parameters */
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.channel = WIFI_CHANNEL_ANY;
	if (kv_store_read(KV_KEY_WIFI_SSID, &wifi_ssid, sizeof(wifi_ssid)) <= 0) {
		LOG_WRN("No WiFi SSID");
		return;
	}
	params.ssid = wifi_ssid.ssid.value;
	params.ssid_length = wifi_ssid.ssid.value_num - 1;
	if (kv_store_read(KV_KEY_WIFI_PSK, &wifi_psk, sizeof(wifi_psk)) <= 0) {
		params.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		params.security = WIFI_SECURITY_TYPE_PSK;
		params.psk = wifi_psk.psk.value;
		params.psk_length = wifi_psk.psk.value_num - 1;
	}

	/* Initiate connection */
	LOG_INF("Initiating connection to '%s'", params.ssid);
	(void)net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_if, &params,
		       sizeof(struct wifi_connect_req_params));
}

static void conn_timeout_worker(struct k_work *work)
{
	/* Cancel the pending connection */
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_if, NULL, 0);

	/* Notify stack of timeout */
	net_mgmt_event_notify(NET_EVENT_CONN_IF_TIMEOUT, wifi_if);
}

static void conn_config_changed_worker(struct k_work *work)
{
	int timeout;

	/* If interface is not requested to be up, nothing to do */
	if (!net_if_is_admin_up(wifi_if)) {
		return;
	}

	/* Configuration changed, trigger a disconnect */
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_if, NULL, 0);
	/* Reschedule connection */
	if (conn_mgr_if_get_flag(wifi_if, CONN_MGR_IF_PERSISTENT)) {
		/* Schedule reconnection attempt */
		k_work_schedule(&conn_create, K_SECONDS(1));
		/* Schedule the timeout if set */
		timeout = conn_mgr_if_get_timeout(wifi_if);
		if (timeout > CONN_MGR_IF_NO_TIMEOUT) {
			k_work_schedule(&conn_timeout, K_SECONDS(timeout));
		}
	}
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
				    struct net_if *iface)
{
	const struct wifi_status *status = cb->info;
	bool persistent = conn_mgr_if_get_flag(wifi_if, CONN_MGR_IF_PERSISTENT);
	int timeout;

	if (iface != wifi_if) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->conn_status == 0) {
			/* Cancel any pending work timeout */
			LOG_INF("Connection successful");
			(void)k_work_cancel_delayable(&conn_timeout);
		} else {
			/* Attempt to schedule the connection again */
			LOG_WRN("Connection failed, retrying (%d)", status->conn_status);
			k_work_schedule(&conn_create, K_SECONDS(1));
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_INF("Connection lost (%d)%s", status->disconn_reason,
			persistent ? ", retrying" : "");
		if (persistent) {
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT
			/* WPA SUPP automatically attempts to reconnect */
#else
			/* Schedule reconnection attempt */
			k_work_schedule(&conn_create, K_SECONDS(1));
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT */
			/* Schedule the timeout if set */
			timeout = conn_mgr_if_get_timeout(wifi_if);
			if (timeout > CONN_MGR_IF_NO_TIMEOUT) {
				k_work_schedule(&conn_timeout, K_SECONDS(timeout));
			}
		} else {
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT
			/* Stop reconnection attempts */
			(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_if, NULL, 0);
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT */
		}
		break;
	}
}

static int wifi_mgmt_connect(struct conn_mgr_conn_binding *const binding)
{
	int timeout;

	/* Schedule the connection */
	k_work_schedule(&conn_create, K_NO_WAIT);
	/* Schedule the timeout if set */
	timeout = conn_mgr_if_get_timeout(binding->iface);
	if (timeout > CONN_MGR_IF_NO_TIMEOUT) {
		k_work_schedule(&conn_timeout, K_SECONDS(timeout));
	}
	/* Return immediately, function is required to be non-blocking */
	return 0;
}

static void conn_terminate_worker(struct k_work *work)
{
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_if, NULL, 0);
}

static int wifi_mgmt_disconnect(struct conn_mgr_conn_binding *const binding)
{
	/* Cancel any pending connection work */
	k_work_cancel_delayable(&conn_create);
	/* Disconnect from the system workqueue */
	k_work_submit(&conn_terminate);
	/* Return immediately, function is required to be non-blocking */
	return 0;
}

static void kv_value_changed(uint16_t key, const void *data, size_t data_len, void *user_ctx)
{
	ARG_UNUSED(data);
	ARG_UNUSED(data_len);
	ARG_UNUSED(user_ctx);

	switch (key) {
	case KV_KEY_WIFI_SSID:
	case KV_KEY_WIFI_PSK:
		LOG_INF("Configuration changed (%d %s)", key, data ? "updated" : "deleted");
		k_work_reschedule(&conn_config_changed, K_MSEC(100));
	}
}

static void wifi_mgmt_init(struct conn_mgr_conn_binding *const binding)
{
	static struct kv_store_cb kv_cb = {
		.value_changed = kv_value_changed,
	};

	wifi_if = binding->iface;
	kv_cb.user_ctx = wifi_if;

	kv_store_register_callback(&kv_cb);

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	k_work_init_delayable(&conn_create, conn_create_worker);
	k_work_init_delayable(&conn_timeout, conn_timeout_worker);
	k_work_init_delayable(&conn_config_changed, conn_config_changed_worker);
	k_work_init(&conn_terminate, conn_terminate_worker);

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT
	net_mgmt_init_event_callback(&wpa_supp_cb, wpa_supp_event_handler,
				     NET_EVENT_SUPPLICANT_READY | NET_EVENT_SUPPLICANT_NOT_READY);
	net_mgmt_add_event_callback(&wpa_supp_cb);
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT */

	/* Optional binding flags */
	conn_mgr_binding_set_flag(binding, CONN_MGR_IF_PERSISTENT,
				  IS_ENABLED(CONFIG_CONN_MGR_WIFI_KV_STORE_PERSISTENT));
	conn_mgr_binding_set_flag(binding, CONN_MGR_IF_NO_AUTO_CONNECT,
				  !IS_ENABLED(CONFIG_CONN_MGR_WIFI_KV_STORE_AUTO_CONNECT));
}

static struct conn_mgr_conn_api l2_wifi_conn_api = {
	.connect = wifi_mgmt_connect,
	.disconnect = wifi_mgmt_disconnect,
	.init = wifi_mgmt_init,
};

CONN_MGR_CONN_DEFINE(CONNECTIVITY_WIFI_MGMT, &l2_wifi_conn_api);
