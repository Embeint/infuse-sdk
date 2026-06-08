/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/reboot.h>

#include "common_file_actions.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_file_write_impl(struct epacket_rx_metadata *rx_meta,
					    uint32_t request_id, uint16_t rpc_id,
					    struct rpc_file_write_request *req)
{
	struct rpc_common_file_actions_ctx ctx = {0};
	struct infuse_rpc_data *data;
	uint32_t remaining;
	uint32_t expected_len, expected_crc;
	enum infuse_littlefs_folder fs_folder;
	uint32_t fs_file, fs_identifier;
	struct net_buf *data_buf;
	uint32_t data_offset;
	uint32_t expected_offset = 0;
	uint8_t action, ack_period;
	size_t var_len;
	int rc = 0;

	action = req->action;
	expected_len = req->data_header.size;
	remaining = req->data_header.size;
	fs_file = req->filename;
	fs_identifier = req->identifier;
	expected_crc = req->file_crc;
	ack_period = req->data_header.rx_ack_period;
	fs_folder = rpc_common_file_actions_folder_from_action(action, req->folder);

	/* Manual validation of possible parameter mismatch in RPC_ID_FILE_WRITE */
	if ((rpc_id == RPC_ID_FILE_WRITE) && (action == RPC_ENUM_FILE_ACTION_FILE_FOR_COPY) &&
	    (req->folder != INFUSE_LFS_FOLDER_COPY)) {
		rc = -EINVAL;
		goto error_no_cleanup;
	}

	/* Start file write process */
	rc = rpc_common_file_actions_start(&ctx, action, expected_len, expected_crc, fs_folder,
					   fs_file, fs_identifier);
	if (rc == FILE_ALREADY_PRESENT) {
		LOG_INF("File already present");
		goto write_done;
	}
	if (rc < 0) {
		LOG_ERR("Failed to prepare for %d (%d)", action, rc);
		goto error;
	}
	LOG_DBG("Receiving %d bytes", expected_len);

	/* Initial ACK to signal readiness */
	rpc_server_ack_data_ready(rx_meta, request_id);

	while (remaining > 0) {
		data_buf = rpc_server_pull_data(request_id, expected_offset, &rc, K_MSEC(500));
		if (data_buf == NULL) {
			goto error;
		}
		var_len = RPC_DATA_VAR_LEN(data_buf);
		if (var_len > remaining) {
			LOG_WRN("Received too much data %d/%d", var_len, remaining);
			net_buf_unref(data_buf);
			rc = -EINVAL;
			goto error;
		}
		data = (void *)data_buf->data;
		data_offset = data->offset;

		/* Write the received data */
		rc = rpc_common_file_actions_write(&ctx, data_offset, data->payload, var_len);
		if (rc < 0) {
			LOG_ERR("Failed to handle offset %08X (%d)", data_offset, rc);
			net_buf_unref(data_buf);
			goto error;
		}

		expected_offset = data_offset + var_len;
		remaining = expected_len - expected_offset;
		net_buf_unref(data_buf);

		/* Handle any acknowledgements required */
		if (remaining > 0) {
			rpc_server_ack_data(rx_meta, request_id, data_offset, ack_period);
		}
	}

	if (ctx.received != expected_len) {
		LOG_ERR("Unexpected length received (%u != %u)", ctx.received, expected_len);
		rc = -EINVAL;
		goto error;
	}
	if ((expected_crc != UINT32_MAX) && (ctx.crc != expected_crc)) {
		LOG_ERR("Unexpected data CRC (%08X != %08X)", ctx.crc, expected_crc);
		rc = -EINVAL;
		goto error;
	}

write_done:
	/* Controller only builds should not defer as we don't want the host to
	 * take any action (rebooting us) until the patching is actually complete.
	 */
	bool defer = IS_ENABLED(CONFIG_BT_CTLR_ONLY) ? false : true;
	bool dfu_reboot = false;

	/* Finish file write process, deferring long operations */
	rc = rpc_common_file_actions_finish(&ctx, defer, &dfu_reboot);
	if (rc < 0) {
		LOG_ERR("Failed to finish %d (%d)", action, rc);
	}

	/* Allocate and return response */
	struct rpc_file_write_response rsp = {
		.recv_len = ctx.received,
		.recv_crc = ctx.crc,
	};
	struct net_buf *response =
		rpc_response_simple_if(rx_meta->interface, rc, &rsp, sizeof(rsp));

	rpc_command_runner_early_response(rx_meta, request_id, rpc_id, response);

	if (rc == 0) {
		/* Perform deferred long operations */
		(void)rpc_common_file_actions_deferred(&ctx, &dfu_reboot);
	}

	if (dfu_reboot) {
#ifdef CONFIG_INFUSE_REBOOT
		/* The response has been queued, perform a short delay to allow the data to be sent
		 * before infuse_reboot_delayed potentially starts shutting down interfaces.
		 */
		k_sleep(K_MSEC(500));
		/* Schedule the reboot in a few seconds time */
		LOG_INF("File action complete, rebooting for DFU");
		infuse_reboot_delayed(INFUSE_REBOOT_DFU, RPC_ID_FILE_WRITE_BASIC, action,
				      K_SECONDS(2));
#else
		LOG_WRN("INFUSE_REBOOT not enabled, cannot reboot");
#endif /* CONFIG_INFUSE_REBOOT */

		/* Give a  */
	}

	return NULL;
error:
	/* Cleanup resources */
	(void)rpc_common_file_actions_error_cleanup(&ctx);

error_no_cleanup:
	/* Allocate and return response */
	struct rpc_file_write_response rsp_err = {
		.recv_len = ctx.received,
		.recv_crc = ctx.crc,
	};

	return rpc_response_simple_if(rx_meta->interface, rc, &rsp_err, sizeof(rsp_err));
}

struct net_buf *rpc_command_file_write(struct net_buf *request)
{
	struct epacket_rx_metadata rx_meta;
	struct rpc_file_write_request write_req;
	uint32_t request_id;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_file_write_request *request_data = (void *)request->data;

		rx_meta = *req_meta;
		write_req = *request_data;
		request_id = request_data->header.request_id;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}

	return rpc_command_file_write_impl(&rx_meta, request_id, RPC_ID_FILE_WRITE, &write_req);
}

struct net_buf *rpc_command_file_write_basic(struct net_buf *request)
{
	struct rpc_file_write_request write_req = {0};
	struct epacket_rx_metadata rx_meta;
	uint32_t request_id;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_file_write_basic_request *basic_req = (void *)request->data;

		rx_meta = *req_meta;
		request_id = basic_req->header.request_id;
		write_req.header = basic_req->header;
		write_req.data_header = basic_req->data_header;
		write_req.action = basic_req->action;
		write_req.file_crc = basic_req->file_crc;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}

	return rpc_command_file_write_impl(&rx_meta, request_id, RPC_ID_FILE_WRITE_BASIC,
					   &write_req);
}
