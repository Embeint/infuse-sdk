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

struct net_buf *rpc_command_heap_stats(struct net_buf *request)
{
	struct rpc_heap_stats_response rsp = {0};
	struct sys_memory_stats heap_stats;
	int max_rsp, max_entries, num_heaps;
	struct net_buf *rsp_buf;
	struct k_heap *heaps;
	int rc;

	/* Allocate response object */
	rsp_buf = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	max_rsp = net_buf_tailroom(rsp_buf);
	max_entries = max_rsp / sizeof(struct rpc_struct_heap_info);

	/* Get the static heaps */
	num_heaps = k_heap_array_get(&heaps);
	for (int i = 0; i < MIN(max_entries, num_heaps); i++) {
		/* Query info */
		rc = sys_heap_runtime_stats_get(&heaps[i].heap, &heap_stats);
		if ((rc != 0) || (heaps[i].heap.init_bytes == 0)) {
			continue;
		}

		/* Push into the response */
		struct rpc_struct_heap_info *info = net_buf_add(rsp_buf, sizeof(*info));

		info->addr = (uintptr_t)&heaps[i];
		info->free_bytes = heap_stats.free_bytes;
		info->allocated_bytes = heap_stats.allocated_bytes;
		info->max_allocated_bytes = heap_stats.max_allocated_bytes;
	}

	/* Return the response */
	return rsp_buf;
}
