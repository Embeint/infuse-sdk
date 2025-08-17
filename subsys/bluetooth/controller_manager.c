/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

static struct rpc_client_ctx ctx;
static struct rpc_file_write_basic_response write_rsp;
static K_SEM_DEFINE(write_done, 0, 1);

LOG_MODULE_REGISTER(bt_ctlr_manager, LOG_LEVEL_INF);

int bt_controller_manager_init(void)
{
	KV_KEY_TYPE(KV_KEY_BLUETOOTH_CTLR_VERSION) bt_ctlr_ver;
	struct rpc_application_info_request req;
	struct rpc_application_info_response *rsp;
	struct net_buf *buf;
	int rc;

	rpc_client_init(&ctx, DEVICE_DT_GET(DT_INST(0, embeint_epacket_hci)), EPACKET_ADDR_ALL);

	rc = rpc_client_command_sync(&ctx, RPC_ID_APPLICATION_INFO, &req, sizeof(req), K_NO_WAIT,
				     K_MSEC(200), &buf);
	if (rc < 0) {
		LOG_ERR("Failed to query version (%d)", rc);
		return rc;
	}

	/* Unregister from callbacks */
	rpc_client_cleanup(&ctx);

	rsp = (void *)buf->data;
	bt_ctlr_ver.application = rsp->application_id;
	bt_ctlr_ver.version.major = rsp->version.major;
	bt_ctlr_ver.version.minor = rsp->version.minor;
	bt_ctlr_ver.version.revision = rsp->version.revision;
	bt_ctlr_ver.version.build_num = rsp->version.build_num;
	net_buf_unref(buf);

	/* Write the version to storage so cloud can sync it */
	(void)KV_STORE_WRITE(KV_KEY_BLUETOOTH_CTLR_VERSION, &bt_ctlr_ver);
	return 0;
}

static void write_file_done(const struct net_buf *buf, void *user_data)
{
	if (buf == NULL) {
		LOG_WRN("Write timed out");
		write_rsp.header.return_code = -ETIMEDOUT;
	} else {
		const struct rpc_file_write_basic_response *rsp = (const void *)buf->data;

		write_rsp = *rsp;
	}

	k_sem_give(&write_done);
}

int bt_controller_manager_file_write_start(uint32_t *context, uint8_t action, size_t image_len)
{
	struct rpc_file_write_basic_request write_req = {
		.data_header =
			{
				.size = image_len,
				.rx_ack_period = 4,
			},
		.action = action,
	};
	int rc;

	rpc_client_init(&ctx, DEVICE_DT_GET(DT_INST(0, embeint_epacket_hci)), EPACKET_ADDR_ALL);

	LOG_INF("Starting write process");
	rc = rpc_client_command_queue(&ctx, RPC_ID_FILE_WRITE_BASIC, &write_req, sizeof(write_req),
				      write_file_done, NULL, K_NO_WAIT, K_SECONDS(10));
	*context = rpc_client_last_request_id(&ctx);
	if (rc < 0) {
		goto end;
	}

	/* Wait for initial ACK */
	rc = rpc_client_ack_wait(&ctx, *context, K_SECONDS(10));
	LOG_DBG("Write prepare complete");
	/* Writes should have a much tighter timeout */
	(void)rpc_client_update_response_timeout(&ctx, *context, K_SECONDS(1));
end:
	if (rc < 0) {
		*context = 0;
		/* Unregister from callbacks */
		rpc_client_cleanup(&ctx);
	}
	return rc;
}

int bt_controller_manager_file_write_next(uint32_t context, uint32_t image_offset,
					  const void *image_chunk, size_t chunk_len)
{
	/* Push data to controller */
	LOG_DBG("Writing offset %08X", image_offset);
	return rpc_client_data_queue(&ctx, context, image_offset, image_chunk, chunk_len);
}

int bt_controller_manager_file_write_finish(uint32_t context, uint32_t *len, uint32_t *crc)
{
	if (context != 0) {
		/* Completion may take many seconds (patching) */
		(void)rpc_client_update_response_timeout(&ctx, context, K_SECONDS(20));

		k_sem_take(&write_done, K_FOREVER);

		*len = write_rsp.recv_len;
		*crc = write_rsp.recv_crc;

		/* Unregister from callbacks */
		rpc_client_cleanup(&ctx);
	}
	LOG_INF("File write finished");
	return write_rsp.header.return_code;
}
