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
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server);

struct wifi_scan_context {
	struct net_mgmt_event_callback cb;
	struct net_buf *response;
	struct k_sem done;
	uint8_t count;
};

static void scan_result_handle(const struct wifi_scan_result *entry, struct net_buf *rsp)
{
	struct rpc_struct_wifi_scan_result scan_result = {0};
	size_t tailroom = net_buf_tailroom(rsp);
	size_t required_size = sizeof(scan_result) + entry->ssid_length;

	if (tailroom < required_size) {
		LOG_WRN("Insufficient space to report %s", entry->ssid);
		return;
	}

	/* Push results into response */
	scan_result.band = entry->band;
	scan_result.channel = entry->channel;
	scan_result.security = entry->security;
	scan_result.rssi = entry->rssi;
	memcpy(scan_result.bssid, entry->mac, entry->mac_length);
	scan_result.ssid_len = entry->ssid_length;
	net_buf_add_mem(rsp, &scan_result, sizeof(scan_result));
	net_buf_add_mem(rsp, entry->ssid, entry->ssid_length);
}

static void scan_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	struct wifi_scan_context *context = CONTAINER_OF(cb, struct wifi_scan_context, cb);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		scan_result_handle(cb->info, context->response);
		context->count += 1;
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&context->done);
		break;
	default:
		break;
	}
}

struct net_buf *rpc_command_wifi_scan(struct net_buf *request)
{
	struct wifi_scan_context context = {0};
	struct rpc_wifi_scan_response rsp = {0};
	struct rpc_wifi_scan_response *rsp_p;
	struct wifi_scan_params params = {0};
	bool manual_up = false;
	struct net_if *iface;
	int rc;

	iface = net_if_get_first_wifi();
	if (iface == NULL) {
		return rpc_response_simple_req(request, -ENODEV, &rsp, sizeof(rsp));
	}

	/* Request interface to come up if it is not already */
	if (!net_if_is_admin_up(iface)) {
		rc = net_if_up(iface);
		if (rc != 0) {
			LOG_ERR("Failed to bring up %s (%d)", iface->if_dev->dev->name, rc);
			return rpc_response_simple_req(request, -ENODEV, &rsp, sizeof(rsp));
		}
		manual_up = true;
	}

	/* Allocate response object */
	context.response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	rsp_p = (void *)context.response->data;

	/* Free command as we don't need it and this command takes a while */
	rpc_command_runner_request_unref(request);

	k_sem_init(&context.done, 0, 1);
	net_mgmt_init_event_callback(&context.cb, scan_event_handler,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&context.cb);

	LOG_INF("Requesting network scan");
	rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(struct wifi_scan_params));
	if (rc != 0) {
		goto done;
	}

	/* Wait for scan to complete */
	k_sem_take(&context.done, K_FOREVER);
	LOG_INF("%s scanned %d networks", __func__, context.count);

	/* Update number of observed networks */
	rsp_p->network_count = context.count;
done:
	/* Remove callback handler */
	net_mgmt_del_event_callback(&context.cb);
	/* Put interface down if we brought it up */
	if (manual_up) {
		(void)net_if_down(iface);
	}
	/* Return the response */
	return context.response;
}
