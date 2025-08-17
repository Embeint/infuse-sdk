/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/net_buf.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/states.h>

struct net_buf *rpc_command_infuse_states_query(struct net_buf *request)
{
	INFUSE_STATES_ARRAY(temp);
	struct rpc_infuse_states_query_request *req = (void *)request->data;
	struct rpc_infuse_states_query_response *rp, rsp = {0};
	struct rpc_struct_infuse_state state;
	struct net_buf *response;
	uint8_t index;
	int timeout;

	/* Get current application states */
	infuse_states_snapshot(temp);

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	rp = (void *)response->data;

	/* Loop over state array */
	for (int i = 0; i < ARRAY_SIZE(temp); i++) {
		while (temp[i]) {
			/* Validate there is space for more states */
			if (net_buf_tailroom(response) < sizeof(state)) {
				goto done;
			}
			/* Find the next state set in the buffer */
			index = __builtin_ffs(temp[i]) - 1;
			/* Clear the state from the next iteration */
			temp[i] ^= (1 << index);

			if (req->offset) {
				/* Skip leading states */
				req->offset -= 1;
				continue;
			}

			state.state = (i * 8 * sizeof(atomic_t)) + index;
			timeout = infuse_state_get_timeout(state.state);
			if (timeout < 0) {
				/* If the states were iterated between the snapshot and now,
				 * there is a chance that a state with one second remaining has
				 * timed out, resulting in an error code.
				 */
				continue;
			}
			state.timeout = timeout;

			/* Push state into response */
			net_buf_add_mem(response, &state, sizeof(state));
		}
	}
done:
	/* Determine number of remaining states */
	for (int i = 0; i < ARRAY_SIZE(temp); i++) {
		rp->remaining += POPCOUNT(temp[i]);
	}

	/* Return the response */
	return response;
}
