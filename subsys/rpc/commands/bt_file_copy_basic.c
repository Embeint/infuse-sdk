/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/bluetooth/conn.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>

#include "common_bt.h"

struct file_copy_ctx {
	struct rpc_file_write_basic_response write_rsp;
	struct k_sem done;
	int rc;
};

LOG_MODULE_DECLARE(rpc_server);

static void command_data_done(const struct net_buf *buf, void *user_data)
{
	struct file_copy_ctx *ctx = user_data;

	if (buf == NULL) {
		LOG_WRN("Write timed out");
		ctx->rc = -ETIMEDOUT;
	} else {
		const struct rpc_file_write_basic_response *rsp = (const void *)buf->data;

		ctx->write_rsp = *rsp;
		ctx->rc = rsp->header.return_code;
	}

	k_sem_give(&ctx->done);
}

static int file_loader(void *user_data, uint32_t offset, void *data, size_t data_len)
{
	const struct flash_area *fa = user_data;

	return flash_area_read(fa, offset, data, data_len);
}

int rpc_command_bt_file_copy_basic_run(struct rpc_bt_file_copy_basic_request *req,
				       struct rpc_bt_file_copy_basic_response *rsp)
{
	const struct device *interface = DEVICE_DT_GET(DT_INST(0, embeint_epacket_bt_central));
	bt_addr_le_t bluetooth_addr = bt_addr_infuse_to_zephyr(&req->peer);
	union epacket_interface_address if_address = {
		.bluetooth = bluetooth_addr,
	};
	struct file_copy_ctx completion_ctx = {0};
	struct rpc_client_ctx client_ctx;
	const struct flash_area *fa;
	struct bt_conn *conn;
	uint32_t request_id;
	size_t work_mem_size;
	uint8_t *work_mem;
	uint8_t partition_id;
	int rc = 0;

	struct rpc_file_write_basic_request write_req = {
		.data_header =
			{
				.size = req->file_len,
				.rx_ack_period = 1,
			},
		.action = req->action,
		.file_crc = req->file_crc,
	};
	struct rpc_client_auto_load_params load_params = {
		.loader = file_loader,
		.total_len = write_req.data_header.size,
		.ack_wait = K_SECONDS(1),
		.ack_period = req->ack_period,
		.pipelining = req->pipelining,
	};

	/* Data source */
	if (req->file_idx != 0) {
		LOG_WRN("Multiple file storage not yet supported");
	}
	partition_id = FIXED_PARTITION_ID(file_partition);

	/* Validate we are connected to the device before starting */
	conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, &bluetooth_addr);
	if (conn == NULL) {
		LOG_WRN("Not connected");
		return -ENOTCONN;
	}
	bt_conn_unref(conn);

	/* Init the RPC client */
	rpc_client_init(&client_ctx, interface, if_address);
	k_sem_init(&completion_ctx.done, 0, 1);

	/* Queue the initiating command */
	rc = rpc_client_command_queue(&client_ctx, RPC_ID_FILE_WRITE_BASIC, &write_req,
				      sizeof(write_req), command_data_done, &completion_ctx,
				      K_NO_WAIT, K_SECONDS(10));
	if (rc < 0) {
		LOG_WRN("Failed to queue initial command");
		goto cleanup;
	}

	request_id = rpc_client_last_request_id(&client_ctx);

	rc = flash_area_open(partition_id, &fa);
	__ASSERT_NO_MSG(rc == 0);
	load_params.user_data = (void *)fa;

	/* Wait for initial ACK */
	rc = rpc_client_ack_wait(&client_ctx, request_id, K_SECONDS(5));
	if (rc < 0) {
		LOG_WRN("Initial ACK not received");
		goto cleanup;
	}

	/* Reduce timeout value for bulk data transfer */
	rc = rpc_client_update_response_timeout(&client_ctx, request_id, K_SECONDS(1));
	if (rc < 0) {
		goto cleanup;
	}

	work_mem = rpc_server_command_working_mem(&work_mem_size);

	/* Push the data through the client, loaded from the flash area */
	rc = rpc_client_data_queue_auto_load(&client_ctx, request_id, 0, work_mem, work_mem_size,
					     &load_params);

	/* Wait for final RPC_RSP */
	rc = k_sem_take(&completion_ctx.done, K_SECONDS(1));
	if (rc == 0) {
		rc = completion_ctx.rc;
	}
	if (rc == 0) {
		if (completion_ctx.write_rsp.recv_len != write_req.data_header.size) {
			LOG_WRN("Unexpected length (%d != %d)", completion_ctx.write_rsp.recv_len,
				write_req.data_header.size);
			rc = -EIO;
		}
		if (completion_ctx.write_rsp.recv_crc != write_req.file_crc) {
			LOG_WRN("Unexpected CRC (%d != %d)", completion_ctx.write_rsp.recv_crc,
				write_req.file_crc);
			rc = -EIO;
		}
	}

cleanup:
	flash_area_close(fa);
	/* Unregister from callbacks */
	rpc_client_cleanup(&client_ctx);

	return rc;
}

struct net_buf *rpc_command_bt_file_copy_basic(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *rsp_interface = req_meta->interface;
	struct rpc_bt_file_copy_basic_request *req = (void *)request->data;
	struct rpc_bt_file_copy_basic_request req_copy = *req;
	struct rpc_bt_file_copy_basic_response rsp = {0};
	int rc = 0;

	/* Request parameters are cached in `req_copy`, free the request */
	rpc_command_runner_request_unref(request);

	/* Run the command */
	rc = rpc_command_bt_file_copy_basic_run(&req_copy, &rsp);

	/* Return the response */
	return rpc_response_simple_if(rsp_interface, rc, &rsp, sizeof(rsp));
}
