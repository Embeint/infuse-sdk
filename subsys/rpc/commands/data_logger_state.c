/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/data_logger/logger.h>

static int data_logger_query(uint8_t logger_id, struct data_logger_state *state)
{
	const struct device *logger;

	switch (logger_id) {
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_flash_map);
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		logger = DEVICE_DT_GET_ONE(embeint_data_logger_exfat);
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
	default:
		return -ENODEV;
	}

	/* Ensure device logger_id properly */
	if (!device_is_ready(logger)) {
		return -EBADF;
	}

	/* Query logger state */
	data_logger_get_state(logger, state);
	return 0;
}

#ifdef CONFIG_INFUSE_RPC_COMMAND_DATA_LOGGER_STATE
struct net_buf *rpc_command_data_logger_state(struct net_buf *request)
{
	struct rpc_data_logger_state_request *req = (void *)request->data;
	struct rpc_data_logger_state_response rsp = {0};
	struct data_logger_state state;
	int rc;

	rc = data_logger_query(req->logger, &state);
	if (rc == 0) {
		rsp.bytes_logged = state.bytes_logged;
		rsp.logical_blocks = state.logical_blocks;
		rsp.physical_blocks = state.physical_blocks;
		rsp.boot_block = state.boot_block;
		rsp.current_block = state.current_block;
		rsp.earliest_block = state.earliest_block;
		rsp.block_size = state.block_size;
		rsp.block_overhead = state.block_overhead;
		rsp.erase_unit = MIN(state.erase_unit, UINT16_MAX);
		rsp.uptime = k_uptime_seconds();
	}
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
#endif /* CONFIG_INFUSE_RPC_COMMAND_DATA_LOGGER_STATE */

#ifdef CONFIG_INFUSE_RPC_COMMAND_DATA_LOGGER_STATE_V2
struct net_buf *rpc_command_data_logger_state_v2(struct net_buf *request)
{
	struct rpc_data_logger_state_v2_request *req = (void *)request->data;
	struct rpc_data_logger_state_v2_response rsp = {0};
	struct data_logger_state state;
	int rc;

	rc = data_logger_query(req->logger, &state);
	if (rc == 0) {
		rsp.bytes_logged = state.bytes_logged;
		rsp.logical_blocks = state.logical_blocks;
		rsp.physical_blocks = state.physical_blocks;
		rsp.boot_block = state.boot_block;
		rsp.current_block = state.current_block;
		rsp.earliest_block = state.earliest_block;
		rsp.block_size = state.block_size;
		rsp.block_overhead = state.block_overhead;
		rsp.erase_unit = state.erase_unit;
		rsp.uptime = k_uptime_seconds();
	}
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
#endif /* CONFIG_INFUSE_RPC_COMMAND_DATA_LOGGER_STATE_V2 */
