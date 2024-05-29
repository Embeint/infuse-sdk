/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include <infuse/rpc/types.h>

#include "../server.h"

LOG_MODULE_DECLARE(rpc_server);

static void populate_common_state(struct net_if *iface, struct rpc_struct_network_state *out)
{
	out->state = net_if_oper_state(iface);
	out->if_flags = iface->if_dev->flags[0];
	out->l2_flags = iface->if_dev->l2->get_flags(iface);
	out->mtu = net_if_get_mtu(iface);

	if (out->state != NET_IF_OPER_UP) {
		/* Extra fields are invalid */
		return;
	}
#if defined(CONFIG_NET_NATIVE_IPV4)
	memcpy(out->ipv4.addr, iface->config.ip.ipv4->unicast[0].address.in_addr.s4_addr, 4);
#endif
#if defined(CONFIG_NET_NATIVE_IPV6)
	memcpy(out->ipv6.addr, iface->config.ip.ipv6->unicast[0].address.in6_addr.s6_addr, 16);
#endif
}

struct net_buf *rpc_command_wifi_state(struct net_buf *request)
{
	struct rpc_wifi_state_response rsp = {0};
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status;
	int rc;

	if (iface == NULL) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Common networking state */
	populate_common_state(iface, &rsp.common);

	/* WiFi state */
	rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		      sizeof(struct wifi_iface_status));
	if (rc == 0) {
		rsp.wifi.state = status.state;
		strncpy(rsp.wifi.ssid, status.ssid, sizeof(rsp.wifi.ssid));
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
