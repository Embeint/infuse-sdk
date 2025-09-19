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
#if defined(CONFIG_NRF_MODEM_LIB)
#include <infuse/lib/lte_modem_monitor.h>
#include <nrf_modem_at.h>
#elif defined(CONFIG_MODEM_CELLULAR)
#include "lte_at_cmd_modem.h"
#else
#error Unsupported AT interface
#endif

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_lte_at_cmd(struct net_buf *request)
{
	struct rpc_lte_at_cmd_request *req = (void *)request->data;
	struct rpc_lte_at_cmd_response rsp = {0};
	struct infuse_rpc_rsp_header *header;
	struct net_buf *rsp_buf;
	size_t tailroom;
	uint8_t *tail;
	int rc;

	/* Request must be NULL terminated */
	if (request->data[request->len - 1] != '\0') {
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}
#ifdef CONFIG_INFUSE_NRF_MODEM_MONITOR
	if (!lte_modem_monitor_is_at_safe()) {
		return rpc_response_simple_req(request, -EAGAIN, &rsp, sizeof(rsp));
	}
#endif
	/* Allocate response object */
	rsp_buf = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	header = (void *)rsp_buf->data;

	tail = net_buf_tail(rsp_buf);
	tailroom = net_buf_tailroom(rsp_buf);

#ifdef CONFIG_NRF_MODEM_LIB
	tail[0] = '\0';
	rc = nrf_modem_at_cmd(tail, tailroom, "%s", req->cmd);
#endif
#ifdef CONFIG_MODEM_CELLULAR
	rc = cellular_modem_at_cmd(tail, tailroom, req->cmd);
#endif
	/* Notify net_buf of how much data was added */
	tail[tailroom - 1] = '\0';
	net_buf_add(rsp_buf, strlen(tail));

	/* Update return code on failure */
	if (rc < 0) {
		header->return_code = rc;
	}
	return rsp_buf;
}
