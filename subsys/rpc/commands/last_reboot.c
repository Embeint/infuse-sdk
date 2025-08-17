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
#include <infuse/common_boot.h>

struct net_buf *rpc_command_last_reboot(struct net_buf *request)
{
	struct rpc_last_reboot_response rsp = {0};
	struct infuse_reboot_state state;
	struct net_buf *response;

	infuse_common_boot_last_reboot(&state);
	rsp.reason = state.reason;
	rsp.epoch_time_source = state.epoch_time_source;
	rsp.epoch_time = state.epoch_time;
	rsp.hardware_flags = state.hardware_reason;
	rsp.uptime = state.uptime;
	switch (state.info_type) {
	case INFUSE_REBOOT_INFO_GENERIC:
		rsp.param_1 = state.info.generic.info1;
		rsp.param_2 = state.info.generic.info2;
		break;
	case INFUSE_REBOOT_INFO_EXCEPTION_BASIC:
		rsp.param_1 = state.info.exception_basic.program_counter;
		rsp.param_2 = state.info.exception_basic.link_register;
		break;
	case INFUSE_REBOOT_INFO_EXCEPTION_ESF:
#ifdef CONFIG_ARM
		rsp.param_1 = state.info.exception_full.basic.pc;
		rsp.param_2 = state.info.exception_full.basic.lr;
#endif /* CONFIG_ARM */
		break;
	case INFUSE_REBOOT_INFO_WATCHDOG:
		rsp.param_1 = state.info.watchdog.info1;
		rsp.param_2 = state.info.watchdog.info2;
		break;
	default:
		/* Unknown info */
		break;
	}
	memcpy(rsp.thread, state.thread_name, sizeof(rsp.thread));

	/* Allocate the response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));

	if (state.info_type == INFUSE_REBOOT_INFO_EXCEPTION_ESF) {
		/* Push the exception stack frame into the response */
		net_buf_add_mem(response, &state.info.exception_full,
				sizeof(state.info.exception_full));
	}
	return response;
}
