/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/reboot.h>
#include <infuse/bluetooth/controller_manager.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include "nrf_modem_delta_dfu.h"
#endif /* CONFIG_NRF_MODEM_LIB */

#include "common_file_actions.h"

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_file_write_basic(struct net_buf *request)
{
	struct rpc_common_file_actions_ctx ctx = {0};
	struct infuse_rpc_data *data;
	struct epacket_rx_metadata rx_meta;
	uint32_t request_id, remaining;
	uint32_t expected_len, expected_crc;
	struct net_buf *data_buf;
	uint32_t data_offset;
	uint32_t expected_offset = 0;
	uint8_t action, ack_period;
	size_t var_len;
	int rc = 0;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_file_write_basic_request *req = (void *)request->data;

		rx_meta = *req_meta;
		request_id = req->header.request_id;
		action = req->action;
		expected_len = req->data_header.size;
		remaining = req->data_header.size;
		expected_crc = req->file_crc;
		ack_period = req->data_header.rx_ack_period;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}

	/* Start file write process */
	rc = rpc_common_file_actions_start(&ctx, action, expected_len, expected_crc);
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
	rpc_server_ack_data_ready(&rx_meta, request_id);

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
			rpc_server_ack_data(&rx_meta, request_id, data_offset, ack_period);
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

	/* Finish file write process, deferring long operations */
	rc = rpc_common_file_actions_finish(&ctx, RPC_ID_FILE_WRITE_BASIC, defer);
	if (rc < 0) {
		LOG_ERR("Failed to finish %d (%d)", action, rc);
	}

	/* Allocate and return response */
	struct rpc_file_write_basic_response rsp = {
		.recv_len = ctx.received,
		.recv_crc = ctx.crc,
	};
	struct net_buf *response = rpc_response_simple_if(rx_meta.interface, rc, &rsp, sizeof(rsp));

	rpc_command_runner_early_response(&rx_meta, request_id, RPC_ID_FILE_WRITE_BASIC, response);

	if (rc == 0) {
		/* Perform deferred long operations */
		(void)rpc_common_file_actions_deferred(&ctx, RPC_ID_FILE_WRITE_BASIC);
	}
	return NULL;
error:
	/* Cleanup resources */
	(void)rpc_common_file_actions_error_cleanup(&ctx);

	/* Allocate and return response */
	struct rpc_file_write_basic_response rsp_err = {
		.recv_len = ctx.received,
		.recv_crc = ctx.crc,
	};

	return rpc_response_simple_if(rx_meta.interface, rc, &rsp_err, sizeof(rsp_err));
}
