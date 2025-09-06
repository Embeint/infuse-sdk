/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/posix/arpa/inet.h>
#include <zephyr/net_buf.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/net/dns.h>

#include <infuse/validation/core.h>
#include <infuse/validation/wifi.h>

#define TEST "WIFI"

struct wifi_scan_context {
	struct net_mgmt_event_callback cb;
	struct k_sem done;
	uint8_t count;
};

struct wifi_connect_context {
	struct net_mgmt_event_callback cb;
	enum wifi_conn_status result;
	struct k_sem done;
};

static const char *const band_to_str[] = {
	[WIFI_FREQ_BAND_2_4_GHZ] = "2.4 GHz",
	[WIFI_FREQ_BAND_5_GHZ] = "  5 GHz",
	[WIFI_FREQ_BAND_6_GHZ] = "  6 GHz",
};

static K_SEM_DEFINE(l4_connected, 0, 1);

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			     struct net_if *iface)
{
	if (event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_connected);
	}
}

static void scan_result_handle(const struct wifi_scan_result *entry)
{
	VALIDATION_REPORT_INFO(TEST, "Band %s Channel %3d RSSI %3d dBm SSID %s ",
			       band_to_str[entry->band], entry->channel, entry->rssi, entry->ssid);
}

static void scan_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	struct wifi_scan_context *context = CONTAINER_OF(cb, struct wifi_scan_context, cb);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		scan_result_handle(cb->info);
		context->count += 1;
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&context->done);
		break;
	default:
		break;
	}
}

static int validation_network_scan(struct net_if *iface)
{
	struct wifi_scan_context context = {0};
	struct wifi_scan_params params = {0};
	int rc;

	k_sem_init(&context.done, 0, 1);
	net_mgmt_init_event_callback(&context.cb, scan_event_handler,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&context.cb);

	VALIDATION_REPORT_INFO(TEST, "Requesting network scan");
	rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(struct wifi_scan_params));
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Network scan request failed (%d)", rc);
		goto done;
	}

	/* Wait for scan to complete */
	k_sem_take(&context.done, K_FOREVER);
	VALIDATION_REPORT_VALUE(TEST, "SSID_SCANNED", "%d", context.count);
done:
	/* Remove callback handler */
	net_mgmt_del_event_callback(&context.cb);
	return rc;
}

static int validation_wifi_sntp(void)
{
	const char *const sntp_server = CONFIG_INFUSE_VALIDATION_SNTP_SERVER;
	char addr_str[INET6_ADDRSTRLEN] = {0};
	struct sntp_time s_time;
	struct sntp_ctx s_ctx;
	struct sockaddr addr;
	socklen_t addrlen;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DNS query for %s", sntp_server);

	/* Get IP address from DNS */
	rc = infuse_sync_dns(sntp_server, 123, AF_INET, SOCK_DGRAM, &addr, &addrlen);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "DNS query failed (%d)", rc);
		return rc;
	}
	inet_ntop(addr.sa_family, &net_sin(&addr)->sin_addr, addr_str, sizeof(addr_str));
	VALIDATION_REPORT_VALUE(TEST, "SNTP_IP", "%s", addr_str);

	rc = sntp_init(&s_ctx, &addr, addrlen);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to init SNTP (%d)", rc);
		return rc;
	}
	VALIDATION_REPORT_INFO(TEST, "Sending SNTP request");
	rc = sntp_query(&s_ctx, 4 * MSEC_PER_SEC, &s_time);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "SNTP query failed (%d)", rc);
	} else {
		VALIDATION_REPORT_VALUE(TEST, "SNTP_TIME", "%lld", s_time.seconds);
	}

	sntp_close(&s_ctx);
	return rc;
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				    struct net_if *iface)
{
	struct wifi_connect_context *context = CONTAINER_OF(cb, struct wifi_connect_context, cb);
	const struct wifi_status *status = cb->info;

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
	case NET_EVENT_WIFI_DISCONNECT_COMPLETE:
		context->result = status->conn_status;
		k_sem_give(&context->done);
		break;
	default:
		VALIDATION_REPORT_INFO(TEST, "Unknown event %08X", mgmt_event);
	}
}

static int validation_network_connect(struct net_if *iface, uint8_t flags)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)CONFIG_INFUSE_VALIDATE_WIFI_SSID,
		.ssid_length = strlen(CONFIG_INFUSE_VALIDATE_WIFI_SSID),
		.psk = (const uint8_t *)CONFIG_INFUSE_VALIDATE_WIFI_PSK,
		.psk_length = strlen(CONFIG_INFUSE_VALIDATE_WIFI_PSK),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};
	struct net_mgmt_event_callback l4_cb;
	struct wifi_iface_status wifi_status;
	struct wifi_connect_context context;
	int rc;

	k_sem_init(&context.done, 0, 1);
	net_mgmt_init_event_callback(&context.cb, wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_CMD_DISCONNECT_COMPLETE);
	net_mgmt_add_event_callback(&context.cb);

	/* Register for callbacks on network connectivity */
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);

	VALIDATION_REPORT_INFO(TEST, "Initiating connection to %s", params.ssid);

	/* Request the connection */
	rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
		      sizeof(struct wifi_connect_req_params));
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Network connect request failed (%d)", rc);
		goto done;
	}
	/* Wait for the result */
	k_sem_take(&context.done, K_FOREVER);
	if (context.result != WIFI_STATUS_CONN_SUCCESS) {
		VALIDATION_REPORT_ERROR(TEST, "Network connection failed (%d)", context.result);
		rc = -EAGAIN;
		goto done;
	}
	VALIDATION_REPORT_INFO(TEST, "Connected to %s", params.ssid);
	VALIDATION_REPORT_INFO(TEST, "Waiting for IP connectivity");
	rc = k_sem_take(&l4_connected, K_SECONDS(10));
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "IP connectivity timed out");
		goto done;
	}
	VALIDATION_REPORT_INFO(TEST, "IP connectivity gained");

	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &wifi_status,
		      sizeof(struct wifi_iface_status));
	if (rc != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to query status (%d)", rc);
		goto disconnect;
	}
	VALIDATION_REPORT_VALUE(TEST, "BAND", "%d", wifi_status.band);
	VALIDATION_REPORT_VALUE(TEST, "CHANNEL", "%d", wifi_status.channel);
	VALIDATION_REPORT_VALUE(TEST, "SECURITY", "%d", wifi_status.security);
	VALIDATION_REPORT_VALUE(TEST, "RSSI", "%d", wifi_status.rssi);

	if (flags & VALIDATION_WIFI_SNTP_QUERY) {
		rc = validation_wifi_sntp();
	}

disconnect:
	VALIDATION_REPORT_INFO(TEST, "Disconnecting from %s", params.ssid);
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	k_sem_take(&context.done, K_FOREVER);

done:
	/* Remove callback handlers */
	net_mgmt_del_event_callback(&context.cb);
	net_mgmt_del_event_callback(&l4_cb);
	return rc;
}

int infuse_validation_wifi(struct net_if *iface, uint8_t flags)
{
	const char *dev_name = iface->if_dev->dev->name;
	bool manual_up = false;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "IFACE=%s", dev_name);

	/* Request interface to come up if it is not already */
	if (!net_if_is_admin_up(iface)) {
		rc = net_if_up(iface);
		if (rc != 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to bring up %s (%d)", dev_name, rc);
			return rc;
		}
		manual_up = true;
	}

	if (flags & VALIDATION_WIFI_SSID_SCAN) {
		if (validation_network_scan(iface) < 0) {
			goto done;
		}
	}

	if (flags & VALIDATION_WIFI_CONNECT) {
		rc = validation_network_connect(iface, flags);
	}

done:

	/* Put interface down if we brought it up */
	if (manual_up) {
		(void)net_if_down(iface);
	}
	return rc;
}
