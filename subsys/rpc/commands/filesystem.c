/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>

#include <infuse/rpc/commands.h>
#include <infuse/rpc/command_runner.h>
#include <infuse/rpc/types.h>
#include <infuse/fs/littlefs.h>

LOG_MODULE_DECLARE(rpc_server, CONFIG_INFUSE_RPC_LOG_LEVEL);

struct ls_ctx {
	struct net_buf *buf;
	struct rpc_filesystem_ls_response *rsp;
	uint8_t skip;
};

static bool ls_cb(enum infuse_littlefs_folder folder, uint32_t file, void *user_data)
{
	struct rpc_struct_filesystem_file_info *info;
	struct infuse_littlefs_metadata meta;
	struct ls_ctx *ctx = user_data;
	int size;

	ctx->rsp->total_files += 1;
	if (ctx->skip) {
		ctx->skip -= 1;
		return true;
	}

	if (net_buf_tailroom(ctx->buf) < sizeof(ctx->rsp->files[0])) {
		/* Don't terminate iteration as we want `total_files` to be correct */
		return true;
	}
	size = infuse_littlefs_file_size(folder, file);
	if (size < 0) {
		ctx->rsp->header.return_code = -EIO;
		return false;
	}
	if (infuse_littlefs_file_metadata(folder, file, &meta) < 0) {
		ctx->rsp->header.return_code = -EIO;
		return false;
	}
	info = net_buf_add(ctx->buf, sizeof(*info));
	info->name = file;
	info->size = size;
	info->metadata.timestamp = meta.timestamp;
	info->metadata.identifier = meta.identifier;
	info->metadata.crc = meta.crc;
	ctx->rsp->contained_files += 1;
	return true;
}

struct net_buf *rpc_command_filesystem_ls(struct net_buf *request)
{
	struct rpc_filesystem_ls_request *req = (void *)request->data;
	struct rpc_filesystem_ls_response rsp = {0};
	struct ls_ctx ctx = {
		.skip = req->skip,
	};
	int rc;

	/* Allocate response object */
	ctx.buf = rpc_response_simple_req(request, 0, &rsp, sizeof(rsp));
	ctx.rsp = (void *)ctx.buf->data;

	/* Iterate over all files in the folder */
	rc = infuse_littlefs_folder_iter(req->folder, ls_cb, &ctx);
	if (rc < 0) {
		ctx.rsp->header.return_code = rc;
	}
	return ctx.buf;
}

struct net_buf *rpc_command_filesystem_rm(struct net_buf *request)
{
	struct rpc_filesystem_rm_request *req = (void *)request->data;
	struct rpc_filesystem_rm_response rsp = {0};
	int rc;

	/* Attempt to delete the requested file */
	rc = infuse_littlefs_file_delete(req->folder, req->file);

	/* Allocate and return the response */
	return rpc_response_simple_req(request, rc, &rsp, sizeof(rsp));
}
