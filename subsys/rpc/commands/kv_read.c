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

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_kv_read(struct net_buf *request)
{
	struct rpc_kv_read_request *req = (void *)request->data;
	struct rpc_kv_read_response rsp;
	struct rpc_struct_kv_store_value *val_hdr;
	struct net_buf *response;
	ssize_t space;

	net_buf_pull(request, sizeof(*req));

	/* Validate input parameters */
	if ((req->num * sizeof(uint16_t)) != request->len) {
		LOG_WRN("%s invalid input", __func__);
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	/* Loop over all structures */
	for (int i = 0; i < req->num; i++) {
		/* Exit if no more space for data */
		if (net_buf_tailroom(response) < sizeof(*val_hdr)) {
			break;
		}
		/* Allocate the value header */
		val_hdr = net_buf_add(response, sizeof(*val_hdr));
		val_hdr->id = net_buf_pull_le16(request);

		/* Check for write only protection */
		val_hdr->len = kv_store_external_write_only(val_hdr->id);
		if (val_hdr->len == 0) {
			/* Read the key value */
			space = net_buf_tailroom(response);
			LOG_DBG("%s reading key %d (max %d)", __func__, val_hdr->id, space);
			val_hdr->len = kv_store_read(val_hdr->id, net_buf_tail(response), space);
			/* Not enough room in buffer for data */
			if (val_hdr->len > space) {
				val_hdr->len = -ENOSPC;
				break;
			}
			/* Data read successful */
			if (val_hdr->len > 0) {
				net_buf_add(response, val_hdr->len);
			}
		}
	}
	return response;
}
