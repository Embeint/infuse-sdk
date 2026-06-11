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
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/reboot.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_reboot(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	struct rpc_reboot_request *req = (void *)request->data;
	struct rpc_reboot_response rsp = {
		.delay_ms = req->delay_ms ? req->delay_ms : 2000,
	};
	uint32_t schedule_delay_ms = MIN(rsp.delay_ms, 500);
	struct net_buf *response;

	/* Allocate the response packet */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	/* Queue the response immediately */
	rpc_command_runner_early_response(req_meta, req->header.request_id, RPC_ID_REBOOT,
					  response);
	LOG_INF("%s: Rebooting in %d ms", __func__, rsp.delay_ms);
	/* Add a delay before scheduling the reboot, as it starts bringing down interfaces */
	k_sleep(K_MSEC(schedule_delay_ms));
	/* Schedule the reboot */
	infuse_reboot_delayed(INFUSE_REBOOT_RPC, (uintptr_t)rpc_command_reboot, 0x00,
			      K_MSEC(rsp.delay_ms - schedule_delay_ms));
	/* Return the response */
	return NULL;
}
