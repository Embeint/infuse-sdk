/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

#include "common_net_query.h"

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_wifi_state(struct net_buf *request)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct rpc_wifi_state_response rsp = {0};
	struct wifi_iface_status status = {0};
	int rc;

	if (iface == NULL) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Common networking state */
	rpc_common_net_query(iface, &rsp.common);

	/* WiFi state */
	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		      sizeof(struct wifi_iface_status));
	if (rc == 0) {
		rsp.wifi.state = status.state;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
		/* String truncation is fine, we don't need the trailing NULL */
		strncpy(rsp.wifi.ssid, status.ssid, sizeof(rsp.wifi.ssid));
#pragma GCC diagnostic pop
		memcpy(rsp.wifi.bssid, status.bssid, sizeof(rsp.wifi.bssid));
		rsp.wifi.band = status.band;
		rsp.wifi.channel = status.channel;
		rsp.wifi.iface_mode = status.iface_mode;
		rsp.wifi.link_mode = status.link_mode;
		rsp.wifi.security = status.security;
		rsp.wifi.rssi = status.rssi;
		rsp.wifi.beacon_interval = status.beacon_interval;
		rsp.wifi.twt_capable = status.twt_capable;
	} else {
		rsp.wifi.state = WIFI_STATE_UNKNOWN;
	}

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
