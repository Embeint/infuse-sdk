/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/net_buf.h>
#include <zephyr/debug/thread_analyzer.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

struct thread_stats_state {
	struct net_buf *pending_buf;
	struct epacket_rx_metadata *rx_metadata;
	struct infuse_rpc_data data_header;
	uint16_t thread_count;
};

static void thread_stats_cb(struct thread_analyzer_info *info, void *user_data)
{
	struct thread_stats_state *state = user_data;
	size_t name_len = strlen(info->name) + 1;
	size_t required_len = sizeof(struct rpc_struct_thread_stats) + name_len;
	struct rpc_struct_thread_stats *stats;

	state->thread_count += 1;
retry:
	if (state->pending_buf == NULL) {
		/* Allocate new data message */
		state->pending_buf =
			epacket_alloc_tx_for_interface(state->rx_metadata->interface, K_FOREVER);
		if (net_buf_tailroom(state->pending_buf) == 0) {
			/* Backend connection has been lost */
			state->pending_buf = NULL;
			net_buf_unref(state->pending_buf);
			return;
		}
		epacket_set_tx_metadata(state->pending_buf, state->rx_metadata->auth, 0x00,
					INFUSE_RPC_DATA, state->rx_metadata->interface_address);
		net_buf_add_mem(state->pending_buf, &state->data_header,
				sizeof(state->data_header));
	}

	if (net_buf_tailroom(state->pending_buf) < required_len) {
		/* Send full buffer, retry */
		epacket_queue(state->rx_metadata->interface, state->pending_buf);
		state->pending_buf = NULL;
		goto retry;
	}

	/* Shove data into buffer */
	stats = net_buf_add(state->pending_buf, required_len);
	stats->stack_size = info->stack_size;
	stats->stack_used = info->stack_used;
#ifdef CONFIG_THREAD_RUNTIME_STATS
	stats->utilization = info->utilization;
#else
	stats->utilization = 0;
#endif /* CONFIG_THREAD_RUNTIME_STATS */
	memcpy(stats->name, info->name, name_len);

	/* Update offset for next packet */
	state->data_header.offset += required_len;
}

struct net_buf *rpc_command_thread_stats(struct net_buf *request)
{
	struct rpc_thread_stats_request *req = (void *)request->data;
	struct epacket_rx_metadata *metadata = net_buf_user_data(request);
	struct rpc_thread_stats_response rsp = {0};
	struct thread_stats_state state = {
		.rx_metadata = metadata,
		.data_header =
			{
				.request_id = req->header.request_id,
			},
	};

	/* Run the analyzer callback */
	thread_analyzer_ud_run(thread_stats_cb, 0, &state);

	/* Flush the last buffer */
	if (state.pending_buf != NULL) {
		epacket_queue(metadata->interface, state.pending_buf);
		state.pending_buf = NULL;
	}

	/* Return the response */
	rsp.num_threads = state.thread_count;
	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
