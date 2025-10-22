/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/time/epoch.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_time_set(struct net_buf *request)
{
	struct rpc_time_set_request *req = (void *)request->data;
	struct rpc_time_set_response rsp = {0};
	struct timeutil_sync_instant sync = {
		.local = k_uptime_ticks(),
		.ref = req->epoch_time,
	};
	int rc;

	/* Set the time reference */
	rc = epoch_time_set_reference(TIME_SOURCE_RPC, &sync);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
