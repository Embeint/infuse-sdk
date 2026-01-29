/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_API_H_
#define INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_API_H_

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include <lfs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct infuse_littlefs_state {
	struct lfs lfs;
	struct k_mutex access;
	const struct flash_area *fa;
	/* Directory number, slash, name, NULL */
	char name_buffer[2 + LFS_NAME_MAX + 1];
};

int lfs_api_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
		 lfs_size_t size);
int lfs_api_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer,
		 lfs_size_t size);
int lfs_api_erase(const struct lfs_config *c, lfs_block_t block);
int lfs_api_sync(const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_FS_LITTLEFS_LITTLEFS_API_H_ */
