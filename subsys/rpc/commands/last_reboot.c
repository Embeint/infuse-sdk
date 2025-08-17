/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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

	infuse_common_boot_last_reboot(&state);
	rsp.reason = state.reason;
	rsp.epoch_time_source = state.epoch_time_source;
	rsp.epoch_time = state.epoch_time;
	rsp.hardware_flags = state.hardware_reason;
	rsp.uptime = state.uptime;
	rsp.param_1 = state.info.exception_basic.program_counter;
	rsp.param_2 = state.info.exception_basic.link_register;
	memcpy(rsp.thread, state.thread_name, sizeof(rsp.thread));

	return rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
}
