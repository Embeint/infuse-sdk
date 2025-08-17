/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/net_buf.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/states.h>

struct net_buf *rpc_command_infuse_states_update(struct net_buf *request)
{
	struct rpc_infuse_states_update_request *req = (void *)request->data;
	struct rpc_infuse_states_update_response rsp = {0};
	size_t expected_len;

	/* Validate provided request */
	expected_len = sizeof(*req) + (req->num * sizeof(struct rpc_struct_infuse_state));
	if (expected_len != request->len) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Loop over provided states */
	for (int i = 0; i < req->num; i++) {
		struct rpc_struct_infuse_state *v = &req->states[i];

		switch (v->timeout) {
		case 0:
			infuse_state_set(v->state);
			break;
		case UINT16_MAX:
			infuse_state_clear(v->state);
			break;
		default:
			infuse_state_set_timeout(v->state, v->timeout);
		}
	}

	/* Allocate and return the response object */
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
