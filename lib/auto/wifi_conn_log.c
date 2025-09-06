/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include <infuse/work_q.h>
#include <infuse/auto/wifi_conn_log.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>
#include <infuse/data_logger/high_level/tdf.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static uint8_t loggers;
static uint8_t log_flags;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				    struct net_if *iface)
{
	/* Log with time if not flushing */
	uint64_t epoch_time = log_flags & AUTO_WIFI_LOG_EVENTS_FLUSH ? 0 : epoch_time_now();
	const struct wifi_status *status = cb->info;
	struct wifi_iface_status if_status = {0};

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		if (status->conn_status == WIFI_STATUS_CONN_SUCCESS) {
			if (!(log_flags & AUTO_WIFI_LOG_CONNECTION)) {
				return;
			}
			struct tdf_wifi_connected tdf = {0};

			/* Query the current interface state */
			(void)net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &if_status,
				       sizeof(struct wifi_iface_status));

			memcpy(tdf.network.bssid, if_status.bssid, sizeof(tdf.network.bssid));
			tdf.network.band = if_status.band;
			tdf.network.channel = if_status.channel;
			tdf.network.iface_mode = if_status.iface_mode;
			tdf.network.link_mode = if_status.link_mode;
			tdf.network.security = if_status.security;
			tdf.network.rssi = if_status.rssi;
			tdf.network.beacon_interval = if_status.beacon_interval;
			tdf.network.twt_capable = if_status.twt_capable;

			TDF_DATA_LOGGER_LOG(loggers, TDF_WIFI_CONNECTED, epoch_time, &tdf);
		} else {
			if (!(log_flags & AUTO_WIFI_LOG_FAILURES)) {
				return;
			}
			struct tdf_wifi_connection_failed tdf = {.reason = status->conn_status};

			TDF_DATA_LOGGER_LOG(loggers, TDF_WIFI_CONNECTION_FAILED, epoch_time, &tdf);
		}
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (!(log_flags & AUTO_WIFI_LOG_DISCONNECTION)) {
			return;
		}
		struct tdf_wifi_disconnected tdf = {.reason = status->disconn_reason};

		TDF_DATA_LOGGER_LOG(loggers, TDF_WIFI_DISCONNECTED, epoch_time, &tdf);
		break;
	}

	/* Flush if requested */
	if (log_flags & AUTO_WIFI_LOG_EVENTS_FLUSH) {
		tdf_data_logger_flush(loggers);
	}
}

void auto_wifi_conn_log_configure(uint8_t tdf_logger_mask, uint8_t flags)
{
	loggers = tdf_logger_mask;
	log_flags = flags;

	/* Callback registration */
	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);
}
