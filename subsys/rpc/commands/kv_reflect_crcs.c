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
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include "../../fs/kv_store/kv_internal.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_kv_reflect_crcs(struct net_buf *request)
{
	struct rpc_kv_reflect_crcs_request *req = (void *)request->data;
	struct rpc_kv_reflect_crcs_response *rp, rsp = {.num = 0, .remaining = KV_REFLECT_NUM};
	struct net_buf *response;
	struct key_value_slot_definition *defs;
	uint16_t idx = 0;
	size_t num;

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	rp = (void *)response->data;

	/* Iterate over slot definitions */
	defs = kv_internal_slot_definitions(&num);
	for (int i = 0; i < num; i++) {
		/* Ignore slots without the reflect flag */
		if (!(defs[i].flags & KV_FLAGS_REFLECT)) {
			continue;
		}
		/* Iterate over every key in slot */
		for (int j = 0; j < defs[i].range; j++) {
			/* Ensure space space exists for more CRCs */
			if (net_buf_tailroom(response) < sizeof(rp->crcs[0])) {
				goto loop_terminate;
			}
			/* Skip first N keys */
			if (idx < req->offset) {
				rp->remaining--;
				idx++;
				continue;
			}
			/* Populate data */
			rp->crcs[rp->num].id = defs[i].key + j;
			rp->crcs[rp->num].crc = kv_reflect_key_crc(idx);
			rp->remaining--;
			rp->num++;
			idx++;

			net_buf_add(response, sizeof(rp->crcs[0]));
		}
	}
loop_terminate:
	return response;
}
