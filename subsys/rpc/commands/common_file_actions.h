/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdlib.h>
#include <stdbool.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>

#include <infuse/rpc/types.h>

struct rpc_common_file_actions_ctx {
	const struct flash_area *fa;
	struct stream_flash_ctx stream_ctx;
	uint32_t client_ctx;
	uint32_t received;
	uint32_t crc;
	enum rpc_enum_file_action action;
	bool needs_cleanup;
};

#define FILE_ALREADY_PRESENT 1

int rpc_common_file_actions_start(struct rpc_common_file_actions_ctx *ctx,
				  enum rpc_enum_file_action action, uint32_t length, uint32_t crc);

int rpc_common_file_actions_write(struct rpc_common_file_actions_ctx *ctx, uint32_t offset,
				  const void *data, size_t data_len);

int rpc_common_file_actions_finish(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id,
				   bool defer_long);

int rpc_common_file_actions_deferred(struct rpc_common_file_actions_ctx *ctx, uint16_t rpc_id);

int rpc_common_file_actions_error_cleanup(struct rpc_common_file_actions_ctx *ctx);
