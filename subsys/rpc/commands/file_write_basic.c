/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>
#include <infuse/rpc/types.h>
#include <infuse/epacket/packet.h>
#include <infuse/reboot.h>

#include "../command_runner.h"
#include "../server.h"

LOG_MODULE_DECLARE(rpc_server);

struct net_buf *rpc_command_file_write_basic(struct net_buf *request)
{
	struct infuse_rpc_data *data;
	const struct device *interface;
	uint32_t request_id, remaining, expected;
	const struct flash_area *fa = NULL;
	struct net_buf *data_buf;
	enum epacket_auth auth;
	uint32_t data_offset;
	uint32_t received = 0;
	uint32_t expected_offset = 0;
	uint8_t action, ack_period;
	size_t var_len;
	uint32_t crc = 0;
	int rc = 0;

	/* Cache data from request and free the buffer.
	 * Scoped in a block to ensure request packet is not used after free.
	 */
	{
		struct epacket_rx_metadata *req_meta = net_buf_user_data(request);
		struct rpc_file_write_basic_request *req = (void *)request->data;

		request_id = req->header.request_id;
		interface = req_meta->interface;
		action = req->action;
		auth = req_meta->auth;
		expected = req->data_header.size;
		remaining = req->data_header.size;
		ack_period = req->data_header.rx_ack_period;

		rpc_command_runner_request_unref(request);
		request = NULL;
	}

	/* Determine action */
	switch (action) {
	case RPC_ENUM_FILE_ACTION_DISCARD:
		break;
#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
		if (rc == 0) {
			rc = infuse_dfu_image_erase(fa, expected);
		}
		break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */
	default:
		rc = -EINVAL;
		goto error;
	}

	LOG_DBG("Receiving %d bytes", remaining);

	/* Initial ACK to signal readiness */
	rpc_server_ack_data_ready(interface, request_id);

	while (remaining > 0) {
		data_buf = rpc_server_pull_data(request_id, expected_offset, K_MSEC(500));
		if (data_buf == NULL) {
			LOG_WRN("Timeout waiting for offset %08X", expected_offset);
			rc = -ETIMEDOUT;
			goto error;
		}
		var_len = RPC_DATA_VAR_LEN(data_buf);
		if (var_len > remaining) {
			LOG_WRN("Received too much data %d/%d", var_len, remaining);
			rc = -EINVAL;
			goto error;
		}
		data = (void *)data_buf->data;
		data_offset = data->offset;
		crc = crc32_ieee_update(crc, data->payload, var_len);

		switch (action) {
#if FIXED_PARTITION_EXISTS(slot1_partition)
		case RPC_ENUM_FILE_ACTION_APP_IMG:
			rc = flash_area_write(fa, data_offset, data->payload, var_len);
			if (rc < 0) {
				LOG_ERR("DFU: Failed to write (%d)", rc);
				goto error;
			}
			break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
		}

		expected_offset = data_offset + var_len;
		remaining = expected - expected_offset;
		received += var_len;
		net_buf_unref(data_buf);

		/* Handle any acknowledgements required */
		if (remaining > 0) {
			rpc_server_ack_data(interface, request_id, data_offset, ack_period);
		}
	}

	/* Close flash area if open */
	if (fa != NULL) {
		flash_area_close(fa);
	}

	/* Post write actions */
	switch (action) {
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		if (boot_request_upgrade_multi(0, BOOT_UPGRADE_TEST) == 0) {
#ifdef CONFIG_INFUSE_REBOOT
			LOG_INF("DFU download complete, rebooting");
			infuse_reboot_delayed(INFUSE_REBOOT_RPC, 0x00, 0x00, K_SECONDS(2));
#else
			LOG_WRN("INFUSE_REBOOT not enabled, cannot reboot");
#endif /* CONFIG_INFUSE_REBOOT */
		}
		break;
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */
	}

	/* Allocate and return response */
	struct rpc_file_write_basic_response rsp_ok = {
		.recv_len = received,
		.recv_crc = crc,
	};

	return rpc_response_simple_if(interface, 0, &rsp_ok, sizeof(rsp_ok));
error:
	/* Close flash area if open */
	if (fa != NULL) {
		flash_area_close(fa);
	}

	/* Allocate and return response */
	struct rpc_file_write_basic_response rsp_err = {
		.recv_len = 0,
		.recv_crc = 0,
	};

	return rpc_response_simple_if(interface, rc, &rsp_err, sizeof(rsp_err));
}
