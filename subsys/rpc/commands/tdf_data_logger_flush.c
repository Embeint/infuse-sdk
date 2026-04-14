/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/toolchain.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

/* Ensure alignment between RPC and data logger definitions */
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_FLASH_ONBOARD ==
	     TDF_DATA_LOGGER_FLASH);
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_FLASH_REMOVABLE ==
	     TDF_DATA_LOGGER_REMOVABLE);
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_SERIAL == TDF_DATA_LOGGER_SERIAL);
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_UDP == TDF_DATA_LOGGER_UDP);
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_BT_ADV == TDF_DATA_LOGGER_BT_ADV);
BUILD_ASSERT((enum tdf_data_logger_mask)RPC_ENUM_TDF_DATA_LOGGER_BT_PERIPH ==
	     TDF_DATA_LOGGER_BT_PERIPHERAL);

struct net_buf *rpc_command_tdf_data_logger_flush(struct net_buf *request)
{
	struct rpc_tdf_data_logger_flush_request *req = (void *)request->data;
	struct rpc_tdf_data_logger_flush_response *rsp_ptr;
	struct rpc_tdf_data_logger_flush_response rsp = {0};
	struct rpc_struct_data_logger_flushed *flushed;
	const struct device *logger;
	struct net_buf *response;
	uint8_t remaining = req->loggers;
	uint8_t mask, offset;

	/* Allocate response object */
	response = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	rsp_ptr = (void *)response->data;

	/* Iterate over requested flushes */
	while (remaining) {
		offset = __builtin_ffs(remaining) - 1;
		mask = 1 << offset;

		logger = tdf_data_logger_from_mask(mask);
		if (logger) {
			/* Flush each logger and record outputs */
			flushed = net_buf_add(response, sizeof(*flushed));
			flushed->logger = mask;
			flushed->num = tdf_data_logger_flush_dev(logger);
			rsp_ptr->num += 1;
		}
		remaining ^= mask;
	}
	return response;
}
