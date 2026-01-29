/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>

#include <infuse/fs/littlefs.h>

#include "littlefs_api.h"
#include "littlefs_util.h"

#define LFS_PARTITION            DT_CHOSEN(infuse_littlefs_partition)
#define LFS_PARTITION_ID         DT_FIXED_PARTITION_ID(LFS_PARTITION)
#define LFS_PARTITION_DEVICE     DT_MEM_FROM_FIXED_PARTITION(LFS_PARTITION)
#define LFS_PARTITION_OFFSET     DT_REG_ADDR(LFS_PARTITION)
#define LFS_PARTITION_SIZE       DT_REG_SIZE(LFS_PARTITION)
#define LFS_PARTITION_BLOCK_SIZE DT_PROP(LFS_PARTITION_DEVICE, erase_block_size)
#define LFS_PARTITION_BLOCK_CNT  (LFS_PARTITION_SIZE / LFS_PARTITION_BLOCK_SIZE)

LOG_MODULE_REGISTER(infuse_littlefs, CONFIG_INFUSE_LITTLEFS_LOG_LEVEL);

static uint8_t lfs_read_buffer[CONFIG_INFUSE_LITTLEFS_CACHE_SIZE];
static uint8_t lfs_prog_buffer[CONFIG_INFUSE_LITTLEFS_CACHE_SIZE];
static uint8_t lfs_lookahead_buffer[CONFIG_INFUSE_LITTLEFS_LOOKAHEAD_SIZE];
static struct infuse_littlefs_state lfs_state;
static const struct lfs_config lfs_cfg = {
	.context = &lfs_state,
	.read = lfs_api_read,
	.prog = lfs_api_prog,
	.erase = lfs_api_erase,
	.sync = lfs_api_sync,
	.read_size = 16,
	.prog_size = 16,
	.block_size = LFS_PARTITION_BLOCK_SIZE,
	.block_count = LFS_PARTITION_BLOCK_CNT,
	.block_cycles = 512,
	.cache_size = CONFIG_INFUSE_LITTLEFS_CACHE_SIZE,
	.lookahead_size = CONFIG_INFUSE_LITTLEFS_LOOKAHEAD_SIZE,
	.read_buffer = lfs_read_buffer,
	.prog_buffer = lfs_prog_buffer,
	.lookahead_buffer = lfs_lookahead_buffer,
};

BUILD_ASSERT(CONFIG_INFUSE_LITTLEFS_LOOKAHEAD_SIZE % 8 == 0);

static char *path_construct(enum infuse_littlefs_folder folder, const char *name)
{
	sprintf(lfs_state.name_buffer, "%d/%s", folder, name);
	return lfs_state.name_buffer;
}

int infuse_littlefs_file_size(enum infuse_littlefs_folder folder, const char *name)
{
	struct lfs_info info;
	char *path;
	int rc;

	k_mutex_lock(&lfs_state.access, K_FOREVER);
	path = path_construct(folder, name);

	rc = lfs_stat(&lfs_state.lfs, path, &info);
	if (rc == 0) {
		rc = info.size;
	} else {
		rc = lfs_to_errno(rc);
	}
	LOG_ERR("%s %d", path, rc);

	k_mutex_unlock(&lfs_state.access);

	return rc;
}

int infuse_littlefs_init(void)
{
	int rc = 0;

	k_mutex_init(&lfs_state.access);
	k_mutex_lock(&lfs_state.access, K_FOREVER);

	/* Obtain the flash area pointer */
	rc = flash_area_open(LFS_PARTITION_ID, &lfs_state.fa);
	if (rc < 0) {
		LOG_ERR("Failed to open flash partition %d", LFS_PARTITION_ID);
		rc = -ENODEV;
		goto out;
	}

	/* Attempt to mount the filesystem */
	rc = lfs_mount(&lfs_state.lfs, &lfs_cfg);
	if (rc < 0) {
		LOG_INF("Initial mount failed, formatting and trying again");

		/* Mounting failed, format and try again */
		rc = lfs_format(&lfs_state.lfs, &lfs_cfg);
		if (rc < 0) {
			LOG_ERR("Failed to format flash area (%d)", rc);
			rc = lfs_to_errno(rc);
			goto out;
		}

		rc = lfs_mount(&lfs_state.lfs, &lfs_cfg);
		if (rc < 0) {
			LOG_ERR("Failed to mount after format (%d)", rc);
			rc = lfs_to_errno(rc);
			goto out;
		}
	}
	LOG_INF("Mounted");

out:
	k_mutex_unlock(&lfs_state.access);
	return rc;
}
