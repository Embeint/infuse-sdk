/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/bluetooth/controller_manager.h>
#include <infuse/cpatch/patch.h>
#include <infuse/dfu/helpers.h>
#include <infuse/reboot.h>
#include <infuse/rpc/commands.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include "nrf_modem_delta_dfu.h"
#endif /* CONFIG_NRF_MODEM_LIB */

#include "common_file_actions.h"

#ifdef CONFIG_INFUSE_DFU_HELPERS
#if FIXED_PARTITION_EXISTS(slot1_partition)
#define SUPPORT_APP_IMG 1
#ifdef CONFIG_INFUSE_CPATCH
#if FIXED_PARTITION_EXISTS(file_partition)
#define SUPPORT_APP_CPATCH 1
#endif /* FIXED_PARTITION_EXISTS(file_partition) */
#endif /* CONFIG_INFUSE_CPATCH*/
#endif /* FIXED_PARTITION_EXISTS(slot1_partition) */
#if FIXED_PARTITION_EXISTS(file_partition)
#define SUPPORT_FILE_COPY 1
#endif /* FIXED_PARTITION_EXISTS(file_partition) */
#endif /* CONFIG_INFUSE_DFU_HELPERS */

LOG_MODULE_DECLARE(rpc_server);

#if defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY)

static void cpatch_watchdog(uint32_t progress, uint32_t total)
{
	ARG_UNUSED(progress);
	ARG_UNUSED(total);

	rpc_server_watchdog_feed();
}

static int flash_area_check_and_erase(struct rpc_common_file_actions_ctx *ctx, uint8_t partition_id,
				      uint32_t length, uint32_t crc, bool mcuboot_trailer)
{
	uint32_t current_crc;
	size_t mem_size;
	uint8_t *mem;
	int rc;

	/* Safe to use this buffer here at the same time as the calling RPC,
	 * as no data has yet been received.
	 */
	mem = rpc_server_command_working_mem(&mem_size);

	/* Check if file contents already match */
	rc = flash_area_open(partition_id, &ctx->fa);
	if (rc == 0) {
		if (crc != UINT32_MAX) {
			rc = flash_area_crc32(ctx->fa, 0, length, &current_crc, mem, mem_size);
			if (current_crc == crc) {
				ctx->crc = crc;
				return FILE_ALREADY_PRESENT;
			}
		}
		/* Limit erase size to flash area size */
		if (length == UINT32_MAX) {
			length = ctx->fa->fa_size;
		}
		/* Erase space for image */
		rc = infuse_dfu_image_erase(ctx->fa, length, cpatch_watchdog, mcuboot_trailer);
		if (rc != 0) {
			/* Close flash area */
			(void)flash_area_close(ctx->fa);
		}
	}
	return rc;
}

#endif /* defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY) */

int rpc_common_file_actions_start(struct rpc_common_file_actions_ctx *ctx,
				  enum rpc_enum_file_action action, uint32_t length, uint32_t crc)
{
	int rc = 0;

	ctx->fa = NULL;
	ctx->action = action;
	ctx->received = 0;
	ctx->crc = 0;

	switch (ctx->action) {
	case RPC_ENUM_FILE_ACTION_DISCARD:
		break;
#ifdef SUPPORT_APP_IMG
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = flash_area_check_and_erase(ctx, FIXED_PARTITION_ID(slot1_partition), length,
						crc, true);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		rc = flash_area_check_and_erase(ctx, FIXED_PARTITION_ID(file_partition), length,
						crc, false);
		break;
#endif /* SUPPORT_APP_CPATCH*/
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		rc = flash_area_check_and_erase(ctx, FIXED_PARTITION_ID(file_partition), length,
						crc, false);
		break;
#endif /* SUPPORT_FILE_COPY*/
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
		rc = bt_controller_manager_file_write_start(&ctx->client_ctx,
							    RPC_ENUM_FILE_ACTION_APP_IMG, length);
		break;
	case RPC_ENUM_FILE_ACTION_BT_CTLR_CPATCH:
		rc = bt_controller_manager_file_write_start(
			&ctx->client_ctx, RPC_ENUM_FILE_ACTION_APP_CPATCH, length);
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

#if defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY)

static int flash_aligned_write(const struct flash_area *fa, uint32_t offset, const void *data,
			       size_t data_len)
{
	const struct flash_parameters *params;
	size_t unaligned, aligned = data_len;
	uint8_t trailing[16];
	int rc = 0;

	/* Get backing flash parameters */
	params = flash_get_parameters(fa->fa_dev);

	__ASSERT(params->write_block_size <= sizeof(trailing),
		 "Required write alignment too large");

	/* Split write into a aligned and unaliged portion */
	unaligned = data_len % params->write_block_size;
	if (unaligned) {
		aligned -= unaligned;
		memset(trailing, params->erase_value, params->write_block_size);
		memcpy(trailing, (uint8_t *)data + aligned, unaligned);
	}

	/* Aligned write first */
	if (aligned) {
		rc = flash_area_write(fa, offset, data, aligned);
	}
	/* Trailing unaligned write second */
	if (unaligned && (rc == 0)) {
		rc = flash_area_write(fa, offset + aligned, trailing, params->write_block_size);
	}
	return rc;
}

#endif /* defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY) */

int rpc_common_file_actions_write(struct rpc_common_file_actions_ctx *ctx, uint32_t offset,
				  const void *data, size_t data_len)
{
	int rc = 0;

	ctx->crc = crc32_ieee_update(ctx->crc, data, data_len);
	ctx->received += data_len;

	switch (ctx->action) {
#ifdef SUPPORT_APP_IMG
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		__fallthrough;
#endif /* SUPPORT_APP_CPATCH */
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = flash_aligned_write(ctx->fa, offset, data, data_len);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		rc = flash_aligned_write(ctx->fa, offset, data, data_len);
		break;
#endif /* SUPPORT_FILE_COPY*/
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
	case RPC_ENUM_FILE_ACTION_BT_CTLR_CPATCH:
		rc = bt_controller_manager_file_write_next(ctx->client_ctx, offset, data, data_len);
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

#ifdef SUPPORT_APP_CPATCH

static int validate_cpatch(struct rpc_common_file_actions_ctx *ctx)
{
	const struct flash_area *fa_original;
	struct cpatch_header header;
	int rc;

	flash_area_open(FIXED_PARTITION_ID(slot0_partition), &fa_original);

	/* Start patch process */
	rc = cpatch_patch_start(fa_original, ctx->fa, &header);
	/* Cleanup files */
	flash_area_close(fa_original);

	return rc;
}

static int finish_cpatch(struct rpc_common_file_actions_ctx *ctx)
{
	const struct flash_area *fa_original, *fa_output;
	struct stream_flash_ctx stream_ctx;
	const struct flash_parameters *params;
	struct cpatch_header header;
	size_t mem_size, out_len;
	uint8_t *mem;
	int rc;

	flash_area_open(FIXED_PARTITION_ID(slot0_partition), &fa_original);
	flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa_output);

	/* Start patch process */
	rc = cpatch_patch_start(fa_original, ctx->fa, &header);
	if (rc < 0) {
		goto cleanup;
	}

#if !defined(CONFIG_STREAM_FLASH_ERASE)

	LOG_INF("Erasing %d bytes of secondary partition", header.output_file.length);
	/* Erase space for image */
	rc = infuse_dfu_image_erase(fa_output, header.output_file.length, cpatch_watchdog, true);
	if (rc < 0) {
		goto cleanup;
	}
	rpc_server_watchdog_feed();
#endif /* !defined(CONFIG_STREAM_FLASH_ERASE) */

	params = flash_get_parameters(fa_output->fa_dev);
	out_len = header.output_file.length;
	/* Stream flash requires output size to be aligned to the write size */
	if (header.output_file.length % params->write_block_size) {
		out_len += params->write_block_size -
			   (header.output_file.length % params->write_block_size);
	}

	/* Safe to use this buffer here at the same time as the calling RPC,
	 * as all data has been written.
	 */
	mem = rpc_server_command_working_mem(&mem_size);
	rc = stream_flash_init(&stream_ctx, FIXED_PARTITION_DEVICE(slot1_partition), mem, mem_size,
			       FIXED_PARTITION_OFFSET(slot1_partition), out_len, NULL);
	__ASSERT_NO_MSG(rc == 0);

	/* Apply the patch */
	LOG_INF("Applying %d byte patch file", header.patch_file.length);
	rc = cpatch_patch_apply(fa_original, ctx->fa, &stream_ctx, &header, cpatch_watchdog);

cleanup:
	/* Cleanup files */
	flash_area_close(fa_output);
	flash_area_close(fa_original);

	return rc;
}

#endif /* SUPPORT_APP_CPATCH */

int rpc_common_file_actions_finish(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id,
				   bool defer_long)
{
	bool reboot = false;
	int rc = 0;

#ifdef CONFIG_INFUSE_DFU_HELPERS
	uint32_t flash_crc;
	size_t mem_size;
	uint8_t *mem;

	/* Temporary memory buffer */
	mem = rpc_server_command_working_mem(&mem_size);

	/* Validate the data written to flash if possible */
	if ((ctx->fa != NULL) && (ctx->received > 0)) {
		rc = flash_area_crc32(ctx->fa, 0, ctx->received, &flash_crc, mem, mem_size);
		if (rc < 0) {
			LOG_ERR("Could not validate written data");
			flash_area_close(ctx->fa);
			return rc;
		} else if (ctx->crc != flash_crc) {
			LOG_ERR("CRC mismatch between received and written (%08X != %08X)",
				ctx->crc, flash_crc);
			flash_area_close(ctx->fa);
			return -EBADE;
		}
	}
#endif /* CONFIG_INFUSE_DFU_HELPERS */

	/* Post write actions */
	switch (ctx->action) {
#ifdef SUPPORT_APP_IMG
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		if (defer_long) {
			/* Patching takes a long time, validate the patch data
			 * but return before applying
			 */
			return validate_cpatch(ctx);
		}
		/* Run the patch apply process */
		rc = finish_cpatch(ctx);
		if (rc < 0) {
			flash_area_close(ctx->fa);
			return rc;
		}
		__fallthrough;
#endif /* SUPPORT_APP_CPATCH */
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Close the flash area */
		flash_area_close(ctx->fa);
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
		if (boot_request_upgrade_multi(0, BOOT_UPGRADE_TEST) == 0) {
			reboot = true;
		}
#else
		LOG_WRN("Cannot request application upgrade");
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		break;
#endif /* SUPPORT_FILE_COPY*/
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
	case RPC_ENUM_FILE_ACTION_BT_CTLR_CPATCH:
		/* Finalise the write */
		rc = bt_controller_manager_file_write_finish(ctx->client_ctx, &ctx->received,
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

int rpc_common_file_actions_deferred(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id)
{
	/* Deferred write actions */
	switch (ctx->action) {
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		/* Run the normal finish logic */
		return rpc_common_file_actions_finish(ctx, rpc_id, false);
#endif /* SUPPORT_APP_CPATCH */
	default:
		return 0;
	}
}

int rpc_common_file_actions_error_cleanup(struct rpc_common_file_actions_ctx *ctx)
{
	int rc = 0;

	switch (ctx->action) {
#ifdef SUPPORT_APP_IMG
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		__fallthrough;
#endif /* SUPPORT_APP_CPATCH */
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Close the flash area */
		flash_area_close(ctx->fa);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		/* Close the flash area */
		flash_area_close(ctx->fa);
		break;
#endif /* SUPPORT_FILE_COPY*/
#ifdef CONFIG_BT_CONTROLLER_MANAGER
	case RPC_ENUM_FILE_ACTION_BT_CTLR_IMG:
	case RPC_ENUM_FILE_ACTION_BT_CTLR_CPATCH:
		/* Finalise the write */
		rc = bt_controller_manager_file_write_finish(ctx->client_ctx, &ctx->received,
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
