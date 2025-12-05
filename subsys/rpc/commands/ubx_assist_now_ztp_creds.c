/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/devicetree.h>
#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/gnss/ubx/modem.h>
#include <infuse/gnss/ubx/protocol.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct cb_ctx {
	struct net_buf *buf;
	uint8_t offset;
};

static int sec_uniqid_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			      size_t payload_len, void *user_data)
{
	struct cb_ctx *ctx = user_data;
	struct rpc_ubx_assist_now_ztp_creds_response *rsp = (void *)ctx->buf->data;

	if (payload_len == sizeof(rsp->ubx_sec_uniqid)) {
		memcpy(rsp->ubx_sec_uniqid, payload, sizeof(rsp->ubx_sec_uniqid));
	} else {
		rsp->header.return_code = -EINVAL;
	}
	return 0;
}

static int mon_ver_handler(uint8_t message_class, uint8_t message_id, const void *payload,
			   size_t payload_len, void *user_data)
{
	struct cb_ctx *ctx = user_data;
	size_t to_copy = payload_len - ctx->offset;

	to_copy = MIN(net_buf_tailroom(ctx->buf), to_copy);
	net_buf_add_mem(ctx->buf, (const uint8_t *)payload + ctx->offset, to_copy);
	return 0;
}

struct net_buf *rpc_command_ubx_assist_now_ztp_creds(struct net_buf *request)
{
	/** Assume that the gnss alias is the appropriate ublox modem */
	const struct device *gnss = DEVICE_DT_GET(DT_ALIAS(gnss));
	struct rpc_ubx_assist_now_ztp_creds_request *req = (void *)request->data;
	struct rpc_ubx_assist_now_ztp_creds_response rsp = {0};
	struct rpc_ubx_assist_now_ztp_creds_response *rsp_ptr;
	struct ubx_modem_data *modem = ubx_modem_data_get(gnss);
	struct cb_ctx ctx = {
		.offset = req->mon_ver_offset,
	};
	int rc;

	if (!device_is_ready(gnss)) {
		return rpc_response_simple_req(request, -ENODEV, &rsp, sizeof(rsp));
	}

	/* Power up the modem */
	rc = pm_device_runtime_get(gnss);
	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	/* Allocate response object (assuming success) */
	ctx.buf = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	rsp_ptr = (void *)ctx.buf->data;

	/* Query and display system version information */
	rc = ubx_modem_send_sync_raw_poll(modem, UBX_MSG_CLASS_SEC, UBX_MSG_ID_SEC_UNIQID,
					  sec_uniqid_handler, &ctx, K_SECONDS(1));
	if (rc != 0) {
		rsp_ptr->header.return_code = rc;
	}
	if (rsp_ptr->header.return_code != 0) {
		/* Command failed or unexpected data */
		goto done;
	}

	/* Query and display system version information */
	rc = ubx_modem_send_sync_raw_poll(modem, UBX_MSG_CLASS_MON, UBX_MSG_ID_MON_VER,
					  mon_ver_handler, &ctx, K_SECONDS(1));
	if (rc != 0) {
		/* Command failed */
		rsp_ptr->header.return_code = rc;
	}

done:
	/* Release power constraint */
	(void)pm_device_runtime_put(gnss);

	/* Return the response */
	return ctx.buf;
}
