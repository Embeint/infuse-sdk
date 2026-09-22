/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <errno.h>

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/commands/kv_write.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/kv_store.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

static struct net_buf *request_validate(struct net_buf *request)
{
	struct rpc_kv_write_request *req = (void *)request->data;
	size_t offset = sizeof(struct rpc_kv_write_request);
	struct rpc_kv_write_response rsp = {0};
	size_t consumed;

	/* Validate write requests don't run off the end of the buffer */
	for (int i = 0; i < req->num; i++) {
		struct rpc_struct_kv_store_value *v = (void *)(request->data + offset);

		if (offset + sizeof(*v) > request->len) {
			LOG_WRN("%s truncated request (idx %d)", __func__, i);
			return rpc_response_simple_req(request, INFUSE_RPC_ERROR_MALFORMED_REQUEST,
						       &rsp, sizeof(rsp));
		}

		if (v->len < 0) {
			LOG_WRN("%s invalid buffer (idx %d key %d len %d)", __func__, i, v->id,
				v->len);
			return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT,
						       &rsp, sizeof(rsp));
		}

		consumed = sizeof(struct rpc_struct_kv_store_value) + v->len;
		offset += consumed;
		if (offset > request->len) {
			LOG_WRN("%s invalid buffer (idx %d key %d len %d)", __func__, i, v->id,
				v->len);
			return rpc_response_simple_req(request, INFUSE_RPC_ERROR_MALFORMED_REQUEST,
						       &rsp, sizeof(rsp));
		}
	}
	if (offset != request->len) {
		LOG_WRN("%s %d trailing request bytes", __func__, request->len - offset);
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_MALFORMED_REQUEST, &rsp,
					       sizeof(rsp));
	}

	return NULL;
}

static int handle_single_write(struct epacket_rx_metadata *meta,
			       struct rpc_struct_kv_store_value *v, struct net_buf *response)
{
	int rc;

	/* Check for read only protection */
	rc = kv_store_external_read_only(v->id);
	if (rc == -EACCES) {
		rc = INFUSE_RPC_ERROR_KV_KEY_NOT_ENABLED;
	} else if (rc < 0) {
		rc = INFUSE_RPC_ERROR_WRITE_PROTECTED;
	}

#ifdef CONFIG_INFUSE_RPC_OPTION_KV_WRITE_APP_VALIDATE
	if (rc == 0) {
		const void *ptr = v->len == 0 ? NULL : v->data;

		/* Run application validation if read only check passed */
		rc = infuse_rpc_command_kv_write_validate(meta, v->id, ptr, v->len)
			     ? 0
			     : INFUSE_RPC_ERROR_KV_WRITE_REJECTED;
	}
#endif /* CONFIG_INFUSE_RPC_OPTION_KV_WRITE_APP_VALIDATE */

	if (rc != 0) {
		goto push_response;
	}

	if (v->len == 0) {
		/* Write the value */
		LOG_DBG("Deleting key %d", v->id);
		rc = kv_store_delete(v->id);
		if (rc < 0) {
			rc = rc == -EACCES ? INFUSE_RPC_ERROR_KV_KEY_NOT_ENABLED
					   : INFUSE_RPC_ERROR_KV_DELETE_FAILED;
		}
	} else {
		/* Write the value */
		LOG_DBG("Writing key %d len %d", v->id, v->len);
		rc = kv_store_write(v->id, v->data, v->len);
		if (rc < 0) {
			rc = rc == -EACCES ? INFUSE_RPC_ERROR_KV_KEY_NOT_ENABLED
					   : INFUSE_RPC_ERROR_KV_WRITE_FAILED;
		}
	}

push_response:
	/* Push response onto the buffer.
	 * If the backend has gone down, we still want to action writes.
	 */
	if (net_buf_tailroom(response) >= sizeof(int16_t)) {
		net_buf_add_le16(response, rc);
	}

	return sizeof(struct rpc_struct_kv_store_value) + v->len;
}

struct net_buf *rpc_command_kv_write(struct net_buf *request)
{
	__maybe_unused struct epacket_rx_metadata *meta = net_buf_user_data(request);
	struct rpc_kv_write_request *req = (void *)request->data;
	struct rpc_kv_write_response rsp;
	struct net_buf *response;
	size_t offset;

	/* Validate the request before actioning */
	response = request_validate(request);
	if (response != NULL) {
		return response;
	}

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	offset = sizeof(struct rpc_kv_write_request);
	/* Loop over all structures */
	for (int i = 0; i < req->num; i++) {
		struct rpc_struct_kv_store_value *v = (void *)(request->data + offset);

		offset += handle_single_write(meta, v, response);
	}
	return response;
}
