/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/zperf.h>

#include <infuse/time/epoch.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_zperf_upload(struct net_buf *request)
{
	struct rpc_zperf_upload_request *req = (void *)request->data;
	struct rpc_zperf_upload_response rsp = {0};
	struct zperf_upload_params params = {0};
	struct zperf_results results = {0};
	uint64_t epoch_time;
	int rc;

	/* Peer address construction */
	if (IS_ENABLED(CONFIG_NET_IPV4) && (req->peer_address.sin_family == AF_INET)) {
		struct sockaddr_in *peer_addr = net_sin(&params.peer_addr);

		peer_addr->sin_family = AF_INET;
		peer_addr->sin_port = req->peer_address.sin_port;
		memcpy(peer_addr->sin_addr.s4_addr, req->peer_address.sin_addr,
		       sizeof(peer_addr->sin_addr.s4_addr));
	} else if (IS_ENABLED(CONFIG_NET_IPV6) && (req->peer_address.sin_family == AF_INET6)) {
		struct sockaddr_in6 *peer_addr = net_sin6(&params.peer_addr);

		peer_addr->sin6_family = AF_INET6;
		peer_addr->sin6_port = req->peer_address.sin_port;
		memcpy(peer_addr->sin6_addr.s6_addr, req->peer_address.sin_addr,
		       sizeof(peer_addr->sin6_addr.s6_addr));
	} else {
		LOG_WRN("%s type %d not supported", "Address", req->peer_address.sin_family);
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Upload request parameters */
	epoch_time = epoch_time_now();
	params.unix_offset_us = (uint64_t)unix_time_from_epoch(epoch_time) * USEC_PER_SEC;
	params.unix_offset_us += epoch_time_milliseconds(epoch_time) * USEC_PER_MSEC;
	params.duration_ms = req->duration_ms;
	params.packet_size = req->packet_size;
	params.rate_kbps = req->rate_kbps ? req->rate_kbps : UINT32_MAX;
	params.options.report_interval_ms = 0;

	if (IS_ENABLED(CONFIG_NET_UDP) && (req->sock_type == SOCK_DGRAM)) {
		LOG_INF("Starting zperf %s upload", "UDP");
		rc = zperf_udp_upload(&params, &results);
	} else if (IS_ENABLED(CONFIG_NET_TCP) && (req->sock_type == SOCK_STREAM)) {
		LOG_INF("Starting zperf %s upload", "TCP");
		rc = zperf_tcp_upload(&params, &results);
	} else {
		LOG_WRN("%s type %d not supported", "Protocol", req->sock_type);
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	if (rc != 0) {
		rc = rc == -1 ? -errno : rc;
		LOG_ERR("Upload failed (%d)", rc);
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}
	LOG_INF("zperf upload complete");

	/* Copy results over to RPC response */
	rsp.nb_packets_sent = results.nb_packets_sent;
	rsp.nb_packets_rcvd = results.nb_packets_rcvd;
	rsp.nb_packets_lost = results.nb_packets_lost;
	rsp.nb_packets_outorder = results.nb_packets_outorder;
	rsp.total_len = results.total_len;
	rsp.time_in_us = results.time_in_us;
	rsp.jitter_in_us = results.jitter_in_us;
	rsp.client_time_in_us = results.client_time_in_us;
	rsp.packet_size = results.packet_size;
	rsp.nb_packets_errors = results.nb_packets_errors;

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
