/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/time/epoch.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_time_get(struct net_buf *request)
{
	struct rpc_time_get_response rsp = {
		.time_source = epoch_time_get_source(),
		.sync_age = epoch_time_reference_age(),
		.epoch_time = epoch_time_now(),
	};

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
