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

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_echo(struct net_buf *request)
{
	struct rpc_echo_request *req = (void *)request->data;
	size_t var_len = RPC_REQUEST_VAR_LEN(request, struct rpc_echo_request);
	struct rpc_echo_response rsp;
	struct net_buf *response;

	LOG_DBG("Echoing %d bytes", var_len);
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	net_buf_add_mem(response, req->array, MIN(net_buf_tailroom(response), var_len));
	return response;
}
