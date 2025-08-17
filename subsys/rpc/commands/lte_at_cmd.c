/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/lib/nrf_modem_monitor.h>

#include <nrf_modem_at.h>

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_lte_at_cmd(struct net_buf *request)
{
	struct rpc_lte_at_cmd_request *req = (void *)request->data;
	struct rpc_lte_at_cmd_response rsp = {0};
	struct net_buf *rsp_buf;
	size_t tailroom;
	uint8_t *tail;
	int rc;

	/* Request must be NULL terminated */
	if (request->data[request->len - 1] != 0x00) {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}
#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR
	if (!nrf_modem_monitor_is_at_safe()) {
		return rpc_response_simple_req(request, -EAGAIN, &rsp, sizeof(rsp));
	}
#endif
	/* Allocate response object */
	rsp_buf = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));

	tail = net_buf_tail(rsp_buf);
	tailroom = net_buf_tailroom(rsp_buf);
	rc = nrf_modem_at_cmd(tail, tailroom, "%s", req->cmd);
	if (rc >= 0) {
		tail[tailroom - 1] = 0x00;
		net_buf_add(rsp_buf, strlen(tail));
	} else {
		/* Update return code */
		struct infuse_rpc_rsp_header *header = (void *)rsp_buf->data;

		header->return_code = rc;
	}
	return rsp_buf;
}
