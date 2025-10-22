/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct net_buf *rpc_command_annotate(struct net_buf *request)
{
	struct rpc_annotate_request *req = (void *)request->data;
	struct rpc_annotate_response rsp = {};
	const struct device *logger;
	uint16_t extra_len = request->len - sizeof(*req);
	int rc = 0;

	switch (req->logger) {
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
		logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_removable));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
#if defined(CONFIG_DATA_LOGGER_EPACKET) && DT_NODE_HAS_STATUS(DT_NODELABEL(tdf_logger_udp), okay)
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
	default:
		return rpc_response_simple_req(request, -ENODEV, &rsp, sizeof(rsp));
	}

	if (!device_is_ready(logger)) {
		return rpc_response_simple_req(request, -EBADF, &rsp, sizeof(rsp));
	}

	if (extra_len == 0) {
		/* No annotation payload */
		return rpc_response_simple_req(request, -EINVAL, &rsp, sizeof(rsp));
	}

	LOG_INF("Annotation: %s @ %u %d", req->annotation, req->timestamp, extra_len);

	/* Relies on the RPC request parameters following the same form as the TDF definition:
	 *     uint32_t gnss_timestamp;
	 *     char event_str[];
	 * Assumption is validated through command testing.
	 */
	tdf_data_logger_log_dev(logger, TDF_ANNOTATION, sizeof(struct tdf_annotation) + extra_len,
				epoch_time_now(), &req->timestamp);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
