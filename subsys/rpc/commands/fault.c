/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>

#include <infuse/reboot.h>
#include <infuse/rpc/types.h>
#include <infuse/time/epoch.h>

#include "../server.h"

LOG_MODULE_DECLARE(rpc_server);

static uint32_t __noinline stack_overflow(uint32_t depth)
{
	uint32_t buffer[64] = {depth, depth + 1, depth + 2, depth + 3};

	if (depth == UINT32_MAX) {
		return 0;
	}
	return buffer[stack_overflow(depth + 1) % ARRAY_SIZE(buffer)];
}

struct net_buf *rpc_command_fault(struct net_buf *request)
{
	struct rpc_fault_request *req = (void *)request->data;
	struct rpc_fault_response rsp = {0};
	int rc = -EINVAL;

	LOG_INF("%s fault code %d", __func__, req->fault);
	switch (req->fault) {
	case K_ERR_STACK_CHK_FAIL:
		rc = stack_overflow(req->zero);
		break;
	case K_ERR_ARM_MEM_DATA_ACCESS:
		/* NULL dereference */
		epoch_time_set_reference(TIME_SOURCE_NONE, (void *)(uintptr_t)req->zero);
		break;
	case K_ERR_ARM_MEM_INSTRUCTION_ACCESS:
		int (*bad_memory)(void) = (void *)0xFFFFAAAA;

		rc = bad_memory();
		break;
	case K_ERR_ARM_USAGE_DIV_0:
		rc = 1000 / req->zero;
		break;
	case K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION:
		__asm__ volatile("udf #255; nop;");
		break;
#ifdef CONFIG_INFUSE_RPC_SERVER_WATCHDOG
	case INFUSE_REBOOT_WATCHDOG:
		/* Blocking */
		k_sleep(K_FOREVER);
		break;
#endif /* CONFIG_INFUSE_RPC_SERVER_WATCHDOG */
	default:
		break;
	}

	/* Allocate the response packet */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
