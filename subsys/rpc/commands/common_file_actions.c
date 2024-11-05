/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/bluetooth/controller_manager.h>
#include <infuse/dfu/helpers.h>
#include <infuse/reboot.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include "nrf_modem_delta_dfu.h"
#endif /* CONFIG_NRF_MODEM_LIB */

#include "common_file_actions.h"
#include "../server.h"

#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
/* Can't use rpc_server_command_working_mem as these functions are used by
 * `coap_download` which already uses that buffer. A smaller buffer here
 * is used for memory efficiency reasons, and only incurs a small overhead
 * on the writing process.
 */
static uint8_t working_mem[128];
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */

LOG_MODULE_DECLARE(rpc_server);

int rpc_common_file_actions_start(struct rpc_common_file_actions_ctx *ctx,
				  enum rpc_enum_file_action action, uint32_t length, uint32_t crc)
{
	const struct flash_area *fa;
	uint32_t current_crc;
	int rc = 0;

	ARG_UNUSED(current_crc);
	ARG_UNUSED(fa);

	ctx->action = action;
	ctx->received = 0;
	ctx->crc = 0;

	switch (ctx->action) {
	case RPC_ENUM_FILE_ACTION_DISCARD:
		break;
#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Setup flash streamer */
		rc = stream_flash_init(&ctx->stream, FIXED_PARTITION_DEVICE(slot1_partition),
				       working_mem, sizeof(working_mem),
				       FIXED_PARTITION_OFFSET(slot1_partition),
				       FIXED_PARTITION_SIZE(slot1_partition), NULL);
		/* Should only fail on invalid parameters */
		__ASSERT_NO_MSG(rc == 0);
		/* Check if file contents already match */
		rc = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
		if (rc == 0) {
			if (crc != UINT32_MAX) {
				rc = flash_area_crc32(fa, 0, length, &current_crc, working_mem,
						      sizeof(working_mem));
				if (current_crc == crc) {
					ctx->crc = crc;
					return FILE_ALREADY_PRESENT;
				}
			}
			/* Erase space for image */
			rc = infuse_dfu_image_erase(fa, length, true);
			/* Close flash area */
			(void)flash_area_close(fa);
		}
		break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
		rc = bt_controller_manager_dfu_write_start(&ctx->client_ctx, length);
		break;
#endif /* CONFIG_BT_CONTROLLER_MANAGER */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = infuse_dfu_nrf91_modem_delta_prepare();
		if (rc > 0) {
			rc = -EIO;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	default:
		rc = -EINVAL;
	}
	return rc;
}

int rpc_common_file_actions_write(struct rpc_common_file_actions_ctx *ctx, uint32_t offset,
				  const void *data, size_t data_len)
{
	int rc = 0;

	ctx->crc = crc32_ieee_update(ctx->crc, data, data_len);
	ctx->received += data_len;

	switch (ctx->action) {
#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = stream_flash_buffered_write(&ctx->stream, data, data_len, false);
		break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
		rc = bt_controller_manager_dfu_write_next(ctx->client_ctx, offset, data, data_len);
		break;
#endif /* CONFIG_BT_CONTROLLER_MANAGER */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = nrf_modem_delta_dfu_write(data, data_len);
		if (rc > 0) {
			rc = -EIO;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	default:
		break;
	}
	return rc;
}

int rpc_common_file_actions_finish(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id)
{
	bool reboot = false;
	int rc = 0;

	ARG_UNUSED(rc);

	/* Post write actions */
	switch (ctx->action) {
#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Flush operations */
		rc = stream_flash_buffered_write(&ctx->stream, NULL, 0, true);
		if (rc == 0) {
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
			if (boot_request_upgrade_multi(0, BOOT_UPGRADE_TEST) == 0) {
				reboot = true;
			}
#else
			LOG_WRN("Cannot request application upgrade");
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */
		}
		break;
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
		/* Finalise the write */
		rc = bt_controller_manager_dfu_write_finish(ctx->client_ctx, &ctx->received,
							    &ctx->crc);
		if (rc == 0) {
			reboot = true;
		}
		break;
#endif /* CONFIG_BT_CONTROLLER_MANAGER */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		rc = infuse_dfu_nrf91_modem_delta_finish();
		if (rc > 0) {
			rc = -EIO;
		}
		if (rc == 0) {
			reboot = true;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	default:
		break;
	}

	if (reboot) {
#ifdef CONFIG_INFUSE_REBOOT
		/* Schedule the reboot in a few seconds time */
		LOG_INF("File action complete, rebooting for DFU");
		infuse_reboot_delayed(INFUSE_REBOOT_DFU, rpc_id, ctx->action, K_SECONDS(2));
#else
		LOG_WRN("INFUSE_REBOOT not enabled, cannot reboot");
#endif /* CONFIG_INFUSE_REBOOT */
	}

	return rc;
}

int rpc_common_file_actions_error_cleanup(struct rpc_common_file_actions_ctx *ctx)
{
	int rc = 0;

	switch (ctx->action) {
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
		/* Finalise the write */
		rc = bt_controller_manager_dfu_write_finish(ctx->client_ctx, &ctx->received,
							    &ctx->crc);
		break;
#endif /* CONFIG_BT_CONTROLLER_MANAGER */
#ifdef CONFIG_NRF_MODEM_LIB
	case RPC_ENUM_FILE_ACTION_NRF91_MODEM_DIFF:
		/* Release modem resources */
		rc = nrf_modem_delta_dfu_write_done();
		if (rc > 0) {
			rc = -EIO;
		}
		break;
#endif /* CONFIG_NRF_MODEM_LIB */
	default:
		break;
	}
	return rc;
}
