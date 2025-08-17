/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/sys/crc.h>

#include <infuse/data_logger/logger.h>
#include <infuse/epacket/packet.h>
#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>

static void erase_progress(uint32_t blocks_erased)
{
	ARG_UNUSED(blocks_erased);

	rpc_server_watchdog_feed();
}

struct net_buf *rpc_command_data_logger_erase(struct net_buf *request)
{
	struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
	const struct device *interface = req_meta->interface;
	struct rpc_data_logger_erase_request *req = (void *)request->data;
	struct rpc_data_logger_erase_response rsp = {0};
	const struct device *logger;
	bool erase_all = req->erase_empty;
	int rc = 0;

	switch (req->logger) {
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
		rc = -ENODEV;
		goto end;
	}

	/* Free command as we no longer need it and this command can take a while */
	rpc_command_runner_request_unref(request);
	req = NULL;
	req_meta = NULL;

	/* Ensure device initialised properly */
	if (!device_is_ready(logger)) {
		rc = -EBADF;
		goto end;
	}

	/* Run the erase */
	rc = data_logger_erase(logger, erase_all, erase_progress);
end:
	return rpc_response_simple_if(interface, rc, &rsp, sizeof(rsp));
}
