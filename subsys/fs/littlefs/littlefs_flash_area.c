/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include "littlefs_api.h"
#include "littlefs_util.h"

int lfs_api_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
		 lfs_size_t size)
{
	struct infuse_littlefs_state *state = c->context;
	size_t offset = block * c->block_size + off;

	int rc = flash_area_read(state->fa, offset, buffer, size);

	return errno_to_lfs(rc);
}

int lfs_api_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer,
		 lfs_size_t size)
{
	struct infuse_littlefs_state *state = c->context;
	size_t offset = block * c->block_size + off;

	int rc = flash_area_write(state->fa, offset, buffer, size);

	return errno_to_lfs(rc);
}

int lfs_api_erase(const struct lfs_config *c, lfs_block_t block)
{
	struct infuse_littlefs_state *state = c->context;
	size_t offset = block * c->block_size;

	int rc = flash_area_flatten(state->fa, offset, c->block_size);

	return errno_to_lfs(rc);
}

int lfs_api_sync(const struct lfs_config *c)
{
	return LFS_ERR_OK;
}
