/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/kv_store.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_kv_write(struct net_buf *request)
{
	struct rpc_kv_write_request *req = (void *)request->data;
	struct rpc_kv_write_response rsp;
	struct net_buf *response;
	size_t offset = sizeof(struct rpc_kv_write_request);
	size_t consumed;
	int16_t rc;

	/* Validate write requests don't run off the end of the buffer */
	for (int i = 0; i < req->num; i++) {
		struct rpc_struct_kv_store_value *v = (void *)(request->data + offset);

		consumed = sizeof(struct rpc_struct_kv_store_value) + v->len;
		offset += consumed;
		if (offset > request->len) {
			LOG_WRN("%s invalid buffer (idx %d key %d len %d)", __func__, i, v->id,
				v->len);
			return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
		}
	}

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	offset = sizeof(struct rpc_kv_write_request);
	/* Loop over all structures */
	for (int i = 0; i < req->num; i++) {
		struct rpc_struct_kv_store_value *v = (void *)(request->data + offset);

		/* Check for write only protection */
		rc = kv_store_external_read_only(v->id);
		if (rc == 0) {
			if (v->len == 0) {
				/* Write the value */
				LOG_DBG("Deleting key %d", v->id);
				rc = kv_store_delete(v->id);
			} else {
				/* Write the value */
				LOG_DBG("Writing key %d len %d", v->id, v->len);
				rc = kv_store_write(v->id, v->data, v->len);
			}
		}
		/* Push response onto the buffer.
		 * If the backend has gone down, we still want to action writes.
		 */
		if (net_buf_tailroom(response) >= sizeof(int16_t)) {
			net_buf_add_le16(response, rc);
		}

		consumed = sizeof(struct rpc_struct_kv_store_value) + v->len;
		offset += consumed;
	}
	return response;
}
