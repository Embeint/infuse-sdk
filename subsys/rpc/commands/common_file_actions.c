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
#include <zephyr/pm/device_runtime.h>

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

static __maybe_unused uint8_t
	common_file_actions_write_buffer[CONFIG_INFUSE_RPC_COMMON_FILE_ACTIONS_WRITE_BUFFER];

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

__maybe_unused static int pm_flash_area_open(uint8_t id, const struct flash_area **fa)
{
	int rc;

	/* Open the flash area */
	rc = flash_area_open(id, fa);
	if (rc == 0) {
		/* Power up the flash device */
		rc = pm_device_runtime_get((*fa)->fa_dev);
	}
	return rc;
}

__maybe_unused static void pm_flash_area_close(const struct flash_area *fa)
{
	/* Power down the flash device */
	(void)pm_device_runtime_put(fa->fa_dev);
	flash_area_close(fa);
}

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
	rc = pm_flash_area_open(partition_id, &ctx->fa);
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
			(void)pm_flash_area_close(ctx->fa);
		}
	}
	return rc;
}

static int common_file_actions_stream_writer_init(struct rpc_common_file_actions_ctx *ctx,
						  uint8_t partition_id,
						  const struct device *partition_dev,
						  off_t partition_offset, size_t file_len,
						  uint32_t crc, bool trailer)
{
	const struct flash_parameters *params = flash_get_parameters(partition_dev);
	int rc;

	/* Setup flash for file to write */
	rc = flash_area_check_and_erase(ctx, partition_id, file_len, crc, trailer);
	if (rc == FILE_ALREADY_PRESENT) {
		return rc;
	} else if (rc < 0) {
		return rc;
	}

	/* Round up write size to write alignment */
	file_len = ROUND_UP(file_len, params->write_block_size);
	return stream_flash_init(&ctx->stream_ctx, partition_dev, common_file_actions_write_buffer,
				 sizeof(common_file_actions_write_buffer), partition_offset,
				 file_len, NULL);
}

#define STREAM_WRITER_INIT(ctx, partition, length, crc, trailer)                                   \
	common_file_actions_stream_writer_init(                                                    \
		ctx, FIXED_PARTITION_ID(partition), FIXED_PARTITION_DEVICE(partition),             \
		FIXED_PARTITION_OFFSET(partition), length, crc, trailer)

#endif /* defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY) */

int rpc_common_file_actions_start(struct rpc_common_file_actions_ctx *ctx,
				  enum rpc_enum_file_action action, uint32_t length, uint32_t crc)
{
	int rc = 0;

	ctx->fa = NULL;
	ctx->stream_ctx.buf_len = 0;
	ctx->client_ctx = 0;
	ctx->action = action;
	ctx->received = 0;
	ctx->crc = 0;
	ctx->needs_cleanup = false;

	switch (ctx->action) {
	case RPC_ENUM_FILE_ACTION_DISCARD:
		break;
#ifdef SUPPORT_APP_IMG
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		rc = STREAM_WRITER_INIT(ctx, slot1_partition, length, crc, true);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		rc = STREAM_WRITER_INIT(ctx, file_partition, length, crc, false);
		break;
#endif /* SUPPORT_APP_CPATCH*/
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		rc = STREAM_WRITER_INIT(ctx, file_partition, length, crc, false);
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

#ifdef CONFIG_INFUSE_DFU_HELPERS
	if (ctx->fa && (rc == 0)) {
		/* Write should proceed, closed by either:
		 *   rpc_common_file_actions_finish or
		 *   rpc_common_file_actions_error_cleanup
		 */
		infuse_dfu_write_erase_start(ctx->fa);
		ctx->needs_cleanup = true;
	}
#endif /* CONFIG_INFUSE_DFU_HELPERS */
	return rc;
}

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
		rc = stream_flash_buffered_write(&ctx->stream_ctx, data, data_len, false);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		rc = stream_flash_buffered_write(&ctx->stream_ctx, data, data_len, false);
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

	pm_flash_area_open(FIXED_PARTITION_ID(slot0_partition), &fa_original);

	/* Start patch process */
	rc = cpatch_patch_start(fa_original, ctx->fa, &header);
	/* Cleanup files */
	pm_flash_area_close(fa_original);

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

	rc = pm_flash_area_open(FIXED_PARTITION_ID(slot0_partition), &fa_original);
	if (rc < 0) {
		/* Nothing to cleanup */
		return rc;
	}
	rc = pm_flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa_output);
	if (rc < 0) {
		/* Close the original FA, nothing else to cleanup */
		pm_flash_area_close(fa_original);
		return rc;
	}
	infuse_dfu_write_erase_start(fa_output);

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
	/* Limit buffer size to common flash erase size */
	mem_size = MIN(mem_size, 4096);
	rc = stream_flash_init(&stream_ctx, FIXED_PARTITION_DEVICE(slot1_partition), mem, mem_size,
			       FIXED_PARTITION_OFFSET(slot1_partition), out_len, NULL);
	__ASSERT_NO_MSG(rc == 0);

	/* Apply the patch */
	LOG_INF("Applying %d byte patch file", header.patch_file.length);
	rc = cpatch_patch_apply(fa_original, ctx->fa, &stream_ctx, &header, cpatch_watchdog);
	LOG_INF("Patching result: %d", rc);

cleanup:
	/* Cleanup files */
	infuse_dfu_write_erase_finish(fa_output);
	pm_flash_area_close(fa_output);
	pm_flash_area_close(fa_original);

	return rc;
}

#endif /* SUPPORT_APP_CPATCH */

int rpc_common_file_actions_finish(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id,
				   bool defer_long)
{
	bool reboot = false;
	int rc = 0;

#if defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY)
	if (ctx->stream_ctx.buf_len &&
	    ((ctx->action == RPC_ENUM_FILE_ACTION_APP_IMG) ||
	     (ctx->action == RPC_ENUM_FILE_ACTION_APP_CPATCH) ||
	     (ctx->action == RPC_ENUM_FILE_ACTION_FILE_FOR_COPY)) &&
	    stream_flash_bytes_buffered(&ctx->stream_ctx)) {
		/* Flush pending bytes to the flash */
		rc = stream_flash_buffered_write(&ctx->stream_ctx, NULL, 0, true);
		if (rc < 0) {
			LOG_ERR("Could not flush remaining data");
			pm_flash_area_close(ctx->fa);
			return rc;
		}
	}
#endif /* defined(SUPPORT_APP_IMG) || defined(SUPPORT_APP_CPATCH) || defined(SUPPORT_FILE_COPY) */

#ifdef CONFIG_INFUSE_DFU_HELPERS
	uint32_t flash_crc;
	size_t mem_size;
	uint8_t *mem;

	if (ctx->needs_cleanup) {
		infuse_dfu_write_erase_finish(ctx->fa);
		ctx->needs_cleanup = false;
	}

	/* Temporary memory buffer */
	mem = rpc_server_command_working_mem(&mem_size);

	/* Validate the data written to flash if possible */
	if ((ctx->fa != NULL) && (ctx->received > 0)) {
		rc = flash_area_crc32(ctx->fa, 0, ctx->received, &flash_crc, mem, mem_size);
		if (rc < 0) {
			LOG_ERR("Could not validate written data");
			pm_flash_area_close(ctx->fa);
			return rc;
		} else if (ctx->crc != flash_crc) {
			LOG_ERR("CRC mismatch between received and written (%08X != %08X)",
				ctx->crc, flash_crc);
			pm_flash_area_close(ctx->fa);
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
			rc = validate_cpatch(ctx);
			if (rc < 0) {
				pm_flash_area_close(ctx->fa);
			}
			return rc;
		}
		/* Run the patch apply process */
		rc = finish_cpatch(ctx);
		if (rc < 0) {
			pm_flash_area_close(ctx->fa);
			return rc;
		}
		__fallthrough;
#endif /* SUPPORT_APP_CPATCH */
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Close the flash area */
		pm_flash_area_close(ctx->fa);
#if defined(CONFIG_MCUBOOT_UPGRADE_ONLY_AUTOMATIC)
		reboot = true;
#elif defined(CONFIG_MCUBOOT_IMG_MANAGER)
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
		/* Close the flash area */
		pm_flash_area_close(ctx->fa);
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

#ifdef CONFIG_INFUSE_DFU_HELPERS
	if (ctx->needs_cleanup) {
		infuse_dfu_write_erase_finish(ctx->fa);
	}
#endif /* CONFIG_INFUSE_DFU_HELPERS */

	switch (ctx->action) {
#ifdef SUPPORT_APP_IMG
#ifdef SUPPORT_APP_CPATCH
	case RPC_ENUM_FILE_ACTION_APP_CPATCH:
		__fallthrough;
#endif /* SUPPORT_APP_CPATCH */
	case RPC_ENUM_FILE_ACTION_APP_IMG:
		/* Close the flash area */
		pm_flash_area_close(ctx->fa);
		break;
#endif /* SUPPORT_APP_IMG */
#ifdef SUPPORT_FILE_COPY
	case RPC_ENUM_FILE_ACTION_FILE_FOR_COPY:
		/* Close the flash area */
		pm_flash_area_close(ctx->fa);
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
