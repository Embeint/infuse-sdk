/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <inttypes.h>

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/types.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);
#define RPC_DURATION_TDF_OFFSET \
	(offsetof(struct rpc_annotate_v2_request, interval_end) + sizeof(uint32_t))

/* The RPC and duration TDF layouts intentionally overlap at this fixed offset. */
BUILD_ASSERT(offsetof(struct rpc_annotate_v2_request, interval_end) ==
		     offsetof(struct rpc_annotate_v2_request, duration) + sizeof(uint32_t));
BUILD_ASSERT(offsetof(struct rpc_annotate_v2_request, label_id) ==
		     RPC_DURATION_TDF_OFFSET + sizeof(uint32_t));
BUILD_ASSERT(offsetof(struct rpc_annotate_v2_request, TDF_IDs) ==
		     RPC_DURATION_TDF_OFFSET + offsetof(struct tdf_annotation_v2_duration, tdf_ids));

static int validate_logger(const struct device **logger, uint16_t tdf_len, uint8_t logger_type)
{
	switch (logger_type) {
#ifdef CONFIG_DATA_LOGGER_FLASH_MAP
	case RPC_ENUM_DATA_LOGGER_FLASH_ONBOARD:
		*logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
		break;
#endif /* CONFIG_DATA_LOGGER_FLASH_MAP */
#ifdef CONFIG_DATA_LOGGER_EXFAT
	case RPC_ENUM_DATA_LOGGER_FLASH_REMOVABLE:
		*logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_removable));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
#if defined(CONFIG_DATA_LOGGER_EPACKET) && DT_NODE_HAS_STATUS(DT_NODELABEL(tdf_logger_udp), okay)
	case RPC_ENUM_DATA_LOGGER_UDP:
		*logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_udp));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
#if defined(CONFIG_DATA_LOGGER_EPACKET) && DT_NODE_HAS_STATUS(DT_NODELABEL(tdf_logger_serial), okay)
	case RPC_ENUM_DATA_LOGGER_SERIAL:
		*logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_serial));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
#if defined(CONFIG_DATA_LOGGER_EPACKET) &&                                                         \
	DT_NODE_HAS_STATUS(DT_NODELABEL(tdf_logger_bt_peripheral), okay)
	case RPC_ENUM_DATA_LOGGER_BT_PERIPHERAL:
		*logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_bt_peripheral));
		break;
#endif /* CONFIG_DATA_LOGGER_EXFAT */
	default:
		return INFUSE_RPC_ERROR_DEVICE_NOT_FOUND;
	}

	if (!device_is_ready(*logger)) {
		return INFUSE_RPC_ERROR_DEVICE_NOT_READY;
	}

	if (tdf_len > UINT8_MAX) {
		/* Too large to represent as a TDF */
		return INFUSE_RPC_ERROR_DATA_UNEXPECTED_LENGTH_RECEIVED;
	}
	return 0;
}

struct net_buf *rpc_command_annotate(struct net_buf *request)
{
	struct rpc_annotate_response rsp = {};
	struct rpc_annotate_request *req = (void *)request->data;
	const struct device *logger;
	int rc = 0;
	uint16_t extra_len = request->len - sizeof(*req);
	uint16_t tdf_len = sizeof(struct tdf_annotation) + extra_len;

	if (extra_len == 0) {
		/* Empty annotation string is not allowed. */
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT, &rsp,
					       sizeof(rsp));
	}

	rc = validate_logger(&logger, tdf_len, req->logger);

	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	/* Relies on the RPC request parameters following the same form as the TDF definition:
	 *     uint32_t gnss_timestamp;
	 *     char event_str[];
	 * Assumption is validated through command testing.
	 */
	rc = tdf_data_logger_log_dev(logger, TDF_ANNOTATION, tdf_len, epoch_time_now(),
				     &req->timestamp);
	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	/* Ensure '%s' always sees a trailing NUL terminator */
	if (net_buf_tailroom(request) > 0) {
		net_buf_add_u8(request, '\0');
	} else {
		req->annotation[extra_len - 1] = '\0';
	}
	LOG_INF("Annotation: %s @ %u %d", req->annotation, req->timestamp, extra_len);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}

struct net_buf *rpc_command_annotate_v2(struct net_buf *request)
{
	struct rpc_annotate_response rsp = {};
	struct rpc_annotate_v2_request *req = (void *)request->data;
	const struct device *logger;
	int rc = 0;
	uint16_t extra_len = request->len - sizeof(*req);
	uint16_t tdf_len;

	switch (req->type) {
	case RPC_ENUM_ANNOTATION_TYPE_INSTANT:
		tdf_len = sizeof(struct tdf_annotation_v2_instant) + extra_len;
		break;
	case RPC_ENUM_ANNOTATION_TYPE_DURATION:
		tdf_len = sizeof(struct tdf_annotation_v2_duration) + extra_len;
		break;
	case RPC_ENUM_ANNOTATION_TYPE_INTERVAL:
		if (req->interval_end == 0 || req->interval_end < req->timestamp) {
			return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT,
						       &rsp, sizeof(rsp));
		}
		tdf_len = sizeof(struct tdf_annotation_v2_interval) + extra_len;
		break;
	default:
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_UNSUPPORTED_REQUEST, &rsp,
					       sizeof(rsp));
	}

	/* All V2 TDFs end with uint16_t TDF IDs. */
	if ((tdf_len % sizeof(uint16_t)) != 0) {
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT, &rsp,
					       sizeof(rsp));
	}

	rc = validate_logger(&logger, tdf_len, req->logger);

	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	switch (req->type) {
	case RPC_ENUM_ANNOTATION_TYPE_INSTANT:
		rc = tdf_data_logger_log_dev(logger, TDF_ANNOTATION_V2_INSTANT, tdf_len,
					     req->timestamp, &req->label_id);
		break;
	case RPC_ENUM_ANNOTATION_TYPE_DURATION:
		{
			/* Temporarily replace the unused interval_end word with duration_ticks. */
			struct tdf_annotation_v2_duration *tdf_duration =
				(void *)((uint8_t *)req + RPC_DURATION_TDF_OFFSET);
			uint32_t temp = tdf_duration->duration_ticks;

			tdf_duration->duration_ticks = req->duration;
			rc = tdf_data_logger_log_dev(logger, TDF_ANNOTATION_V2_DURATION, tdf_len,
					      req->timestamp, tdf_duration);
			tdf_duration->duration_ticks = temp;
		}
		break;
	case RPC_ENUM_ANNOTATION_TYPE_INTERVAL:
		rc = tdf_data_logger_log_dev(logger, TDF_ANNOTATION_V2_INTERVAL, tdf_len,
					     req->timestamp, &req->interval_end);
		break;
	}

	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}

struct net_buf *rpc_command_annotation_register(struct net_buf *request)
{
	struct rpc_annotate_response rsp = {};
	struct rpc_annotation_register_request *req = (void *)request->data;
	const struct device *logger;
	int rc = 0;
	uint16_t extra_len = request->len - sizeof(*req);
	uint16_t tdf_len = sizeof(struct tdf_annotation_v2_label_mapping) + extra_len;

	if (extra_len == 0) {
		/* Empty annotation string is not allowed. */
		return rpc_response_simple_req(request, INFUSE_RPC_ERROR_INVALID_ARGUMENT, &rsp,
					       sizeof(rsp));
	}

	rc = validate_logger(&logger, tdf_len, req->logger);

	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	rc = tdf_data_logger_log_dev(logger, TDF_ANNOTATION_V2_LABEL_MAPPING, tdf_len,
				     req->timestamp, &req->label_id);

	if (rc < 0) {
		return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
	}

	/* Ensure '%s' always sees a trailing NUL terminator */
	if (net_buf_tailroom(request) > 0) {
		net_buf_add_u8(request, '\0');
	} else {
		req->label[extra_len - 1] = '\0';
	}
	LOG_INF("Registered Annotation: %s @ %" PRId64 " %d", req->label, req->timestamp,
		extra_len);

	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
