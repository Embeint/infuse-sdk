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
#include <zephyr/sys/crc.h>

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

enum {
	LFS_STATE_MOUNTED = 0,
	LFS_STATE_FILE_WRITE_OPENED = 1,
	LFS_STATE_FILE_READ_OPENED = 2,
};

#define LFS_STATE_FILE_ANY_OPENED                                                                  \
	(BIT(LFS_STATE_FILE_WRITE_OPENED) | BIT(LFS_STATE_FILE_READ_OPENED))

LOG_MODULE_REGISTER(infuse_littlefs, CONFIG_INFUSE_LITTLEFS_LOG_LEVEL);

static uint8_t lfs_read_buffer[CONFIG_INFUSE_LITTLEFS_CACHE_SIZE];
static uint8_t lfs_prog_buffer[CONFIG_INFUSE_LITTLEFS_CACHE_SIZE];
static uint8_t lfs_lookahead_buffer[CONFIG_INFUSE_LITTLEFS_LOOKAHEAD_SIZE];
static struct infuse_littlefs_state lfs_state;
static sys_slist_t cb_list = SYS_SLIST_STATIC_INIT(&cb_list);
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

static char *folder_path_construct(enum infuse_littlefs_folder folder)
{
	sprintf(lfs_state.name_buffer, "%d", folder);
	return lfs_state.name_buffer;
}

static char *file_path_construct(enum infuse_littlefs_folder folder, uint32_t name)
{
	sprintf(lfs_state.name_buffer, "%d/%x", folder, name);
	return lfs_state.name_buffer;
}

int infuse_littlefs_file_size(enum infuse_littlefs_folder folder, uint32_t file)
{
	struct lfs_info info;
	char *path;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		/* File already opened in the state */
		return -EINVAL;
	}

	k_mutex_lock(&lfs_state.access, K_FOREVER);
	path = file_path_construct(folder, file);
	rc = lfs_stat(&lfs_state.lfs, path, &info);
	if (rc == 0) {
		rc = info.size;
	} else {
		rc = lfs_to_errno(rc);
	}

	k_mutex_unlock(&lfs_state.access);
	return rc;
}

int infuse_littlefs_file_metadata(enum infuse_littlefs_folder folder, uint32_t file,
				  struct infuse_littlefs_metadata *metadata)
{
	char *path;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		/* File already opened in the state */
		return -EINVAL;
	}

	k_mutex_lock(&lfs_state.access, K_FOREVER);
	path = file_path_construct(folder, file);
	rc = lfs_getattr(&lfs_state.lfs, path, INFUSE_LITTLEFS_METADATA_TYPE, metadata,
			 sizeof(*metadata));
	if (rc == sizeof(*metadata)) {
		rc = 0;
	} else if (rc < 0) {
		rc = lfs_to_errno(rc);
	} else {
		rc = -EINVAL;
	}

	k_mutex_unlock(&lfs_state.access);
	return rc;
}

int infuse_littlefs_folder_iter(enum infuse_littlefs_folder folder, infuse_littlefs_file_cb cb,
				void *user_data)
{
	struct lfs_info info;
	uint32_t file;
	lfs_dir_t dir;
	char *path;
	int rc2;
	int rc;

	if (cb == NULL) {
		return -EINVAL;
	}
	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		/* File already opened in the state */
		return -EINVAL;
	}

	k_mutex_lock(&lfs_state.access, K_FOREVER);

	/* Open directory for iteration */
	path = folder_path_construct(folder);
	rc = lfs_dir_open(&lfs_state.lfs, &dir, path);
	if (rc < 0) {
		if (rc == LFS_ERR_NOENT) {
			LOG_INF("Directory '%s' does not exist", path);
		} else {
			LOG_ERR("Failed to open directory '%s' (%d)", path, rc);
		}
		goto unlock;
	}

	LOG_DBG("Iterating directory %d '%s'", folder, path);
	while (true) {
		/* Read next entry in the directory */
		rc = lfs_dir_read(&lfs_state.lfs, &dir, &info);
		if (rc < 0) {
			LOG_ERR("Failed to iterate directory (%d)", rc);
			break;
		} else if (rc == 0) {
			break;
		}

		if (info.type != LFS_TYPE_REG) {
			LOG_DBG("'%s' not a file (%d)", info.name, info.type);
			continue;
		}

		/* Parse string filename back to integer */
		file = strtoul(info.name, NULL, 16);
		LOG_DBG("Found file '%d/%x'", folder, file);

		/* Run the callback */
		if (!cb(folder, file, user_data)) {
			break;
		}
	}

	/* Close directory */
	rc2 = lfs_dir_close(&lfs_state.lfs, &dir);
	if (rc2 < 0) {
		LOG_WRN("Failed to close directory (%d)", rc2);
	}
unlock:
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_open(enum infuse_littlefs_folder folder, uint32_t file)
{
	const int flags = LFS_O_RDONLY;
	char *path;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		/* File already opened in the state */
		return -EINVAL;
	}
	k_mutex_lock(&lfs_state.access, K_FOREVER);

	lfs_state.open.folder = folder;
	lfs_state.open.file = file;

	path = file_path_construct(folder, file);
	LOG_DBG("Opening file '%s'", path);
	rc = lfs_file_opencfg(&lfs_state.lfs, &lfs_state.open.file_obj, path, flags,
			      &lfs_state.open.config);
	if (rc < 0) {
		/* Creation failed, file is invalid */
		goto error;
	}

	atomic_set_bit(&lfs_state.state, LFS_STATE_FILE_READ_OPENED);
	return 0;

error:
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_read(void *data, size_t data_len)
{
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_FILE_READ_OPENED)) {
		return -EINVAL;
	}

	LOG_DBG("Reading %d bytes", data_len);
	rc = lfs_file_read(&lfs_state.lfs, &lfs_state.open.file_obj, data, data_len);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_seek(uint32_t offset)
{
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_FILE_READ_OPENED)) {
		return -EINVAL;
	}

	LOG_DBG("Setting offset to %d bytes", offset);
	rc = lfs_file_seek(&lfs_state.lfs, &lfs_state.open.file_obj, offset, LFS_SEEK_SET);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_create(enum infuse_littlefs_folder folder, uint32_t file,
				struct infuse_littlefs_metadata *metadata)
{
	const int flags = LFS_O_CREAT | LFS_O_EXCL | LFS_O_WRONLY;
	struct lfs_info info;
	char *path;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		/* File already opened in the state */
		return -EINVAL;
	}

	k_mutex_lock(&lfs_state.access, K_FOREVER);

	path = folder_path_construct(folder);
	rc = lfs_stat(&lfs_state.lfs, path, &info);
	if (rc != 0) {
		LOG_DBG("Creating folder '%s'", path);
		rc = lfs_mkdir(&lfs_state.lfs, path);
		if (rc != 0) {
			LOG_WRN("Failed to create folder '%s'", path);
			goto error;
		}
	}

	lfs_state.open.folder = folder;
	lfs_state.open.file = file;
	lfs_state.open.infuse_attr.buffer = metadata;
	lfs_state.open.infuse_attr.size = sizeof(*metadata);

	path = file_path_construct(folder, file);
	LOG_DBG("Creating file '%s'", path);
	rc = lfs_file_opencfg(&lfs_state.lfs, &lfs_state.open.file_obj, path, flags,
			      &lfs_state.open.config);
	if (rc < 0) {
		/* Creation failed, file is invalid */
		goto error;
	}
	atomic_set_bit(&lfs_state.state, LFS_STATE_FILE_WRITE_OPENED);
	return 0;

error:
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_write(const void *data, size_t data_len)
{
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_FILE_WRITE_OPENED)) {
		return -EINVAL;
	}

	LOG_DBG("Writing %d bytes", data_len);
	rc = lfs_file_write(&lfs_state.lfs, &lfs_state.open.file_obj, data, data_len);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_close(void)
{
	struct infuse_littlefs_cb *cb;
	bool was_write = false;
	int rc;

	if (!(atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED)) {
		return -EINVAL;
	}
	if (atomic_test_and_clear_bit(&lfs_state.state, LFS_STATE_FILE_WRITE_OPENED)) {
		struct infuse_littlefs_metadata *meta = lfs_state.open.infuse_attr.buffer;

		was_write = true;
		LOG_DBG("Committing file (ID %08x, Timestamp %d, CRC %08x)", meta->identifier,
			meta->timestamp, meta->crc);
	}

	atomic_clear_bit(&lfs_state.state, LFS_STATE_FILE_READ_OPENED);
	rc = lfs_file_close(&lfs_state.lfs, &lfs_state.open.file_obj);
	if (was_write && (rc == 0)) {
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->file_created) {
				cb->file_created(lfs_state.open.folder, lfs_state.open.file,
						 lfs_state.open.infuse_attr.buffer, cb->user_data);
			}
		}
	}
	lfs_state.open.infuse_attr.buffer = NULL;
	lfs_state.open.infuse_attr.size = 0;
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

int infuse_littlefs_file_delete(enum infuse_littlefs_folder folder, uint32_t file)
{
	struct infuse_littlefs_cb *cb;
	char *path;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		return -EINVAL;
	}
	k_mutex_lock(&lfs_state.access, K_FOREVER);

	path = file_path_construct(folder, file);
	LOG_DBG("Deleting file '%s'", path);
	rc = lfs_remove(&lfs_state.lfs, path);
	if (rc == 0) {
		SYS_SLIST_FOR_EACH_CONTAINER(&cb_list, cb, node) {
			if (cb->file_deleted) {
				cb->file_deleted(folder, file, cb->user_data);
			}
		}
	}
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

#ifdef CONFIG_CRC
int infuse_littlefs_file_crc32(enum infuse_littlefs_folder folder, uint32_t file, uint32_t max_len,
			       uint32_t *crc32_ieee, void *working_mem, size_t working_mem_len)
{
	uint32_t crc = 0x00;
	uint32_t to_read;
	int rc;

	rc = infuse_littlefs_file_open(folder, file);
	if (rc < 0) {
		return rc;
	}

	while (max_len > 0) {
		to_read = MIN(max_len, working_mem_len);

		rc = infuse_littlefs_file_read(working_mem, to_read);
		if (rc <= 0) {
			break;
		}
		/* Update CRC */
		crc = crc32_ieee_update(crc, working_mem, rc);

		max_len -= rc;
		rc = 0;
	}

	*crc32_ieee = crc;
	(void)infuse_littlefs_file_close();
	return rc;
}
#endif

int infuse_littlefs_fs_info(struct infuse_littlefs_fs_info *info)
{
	struct lfs_fsinfo fsinfo;
	int rc;

	if (!atomic_test_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		/* Filesystem not mounted */
		return -EAGAIN;
	}
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		return -EINVAL;
	}

	k_mutex_lock(&lfs_state.access, K_FOREVER);

	rc = lfs_fs_stat(&lfs_state.lfs, &fsinfo);
	if (rc < 0) {
		LOG_WRN("Failed to query filesystem info (%d)", rc);
		goto unlock;
	}
	info->disk_version = fsinfo.disk_version;
	info->block_size = fsinfo.block_size;
	info->block_count = fsinfo.block_count;

	rc = lfs_fs_size(&lfs_state.lfs);
	if (rc < 0) {
		LOG_WRN("Failed to query filesystem size (%d)", rc);
		goto unlock;
	}
	info->blocks_used = rc;
	rc = 0;

unlock:
	k_mutex_unlock(&lfs_state.access);
	return lfs_to_errno(rc);
}

int infuse_littlefs_init(void)
{
	int rc = 0;

	atomic_clear(&lfs_state.state);
	k_mutex_init(&lfs_state.access);
	k_mutex_lock(&lfs_state.access, K_FOREVER);
	memset(&lfs_state.open.file_obj, 0x00, sizeof(lfs_state.open.file_obj));

	/* Config struct needs to remain valid while file is open, so cache it internally */
	lfs_state.open.infuse_attr.type = INFUSE_LITTLEFS_METADATA_TYPE;
	lfs_state.open.config.attrs = &lfs_state.open.infuse_attr;
	lfs_state.open.config.attr_count = 1;

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
	atomic_set_bit(&lfs_state.state, LFS_STATE_MOUNTED);
	LOG_INF("Mounted");

out:
	k_mutex_unlock(&lfs_state.access);
	return rc;
}

void infuse_littlefs_register_cb(struct infuse_littlefs_cb *cb)
{
	sys_slist_append(&cb_list, &cb->node);
}

#ifdef CONFIG_ZTEST

void infuse_littlefs_reset(void)
{
	sys_slist_init(&cb_list);
	if (atomic_get(&lfs_state.state) & LFS_STATE_FILE_ANY_OPENED) {
		(void)lfs_file_close(&lfs_state.lfs, &lfs_state.open.file_obj);
	}
	if (atomic_test_and_clear_bit(&lfs_state.state, LFS_STATE_MOUNTED)) {
		(void)lfs_unmount(&lfs_state.lfs);
	}
	memset(&lfs_state, 0x00, sizeof(lfs_state));
}

#endif /* CONFIG_ZTEST */
