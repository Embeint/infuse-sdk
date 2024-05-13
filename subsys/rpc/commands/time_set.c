/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/types.h>
#include <infuse/time/civil.h>

#include "../server.h"

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_time_set(struct net_buf *request)
{
	struct rpc_time_set_request *req = (void *)request->data;
	struct rpc_time_set_response rsp = {0};
	struct timeutil_sync_instant sync = {
		.local = k_uptime_ticks(),
		.ref = req->civil_time,
	};
	int rc;

	/* Set the time reference */
	rc = civil_time_set_reference(TIME_SOURCE_RPC, &sync);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
