/**
 * @file
 * @copyright 2024 Embeint Inc
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

struct net_buf *rpc_command_time_get(struct net_buf *request)
{
	struct rpc_time_get_response rsp = {
		.time_source = civil_time_get_source(),
		.sync_age = civil_time_reference_age(),
		.civil_time = civil_time_now(),
	};

	/* Allocate and return the response */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
