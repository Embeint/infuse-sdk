/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_exfat

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/exfat.h>
#include <infuse/time/epoch.h>
#include <infuse/identifiers.h>

#include <ff.h>

#include "common.h"
#include "exfat_common.h"

#define FILESYSTEM_LABEL "INFUSE"

#define BLOCKS_PER_FILE (CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE / DATA_LOGGER_EXFAT_BLOCK_SIZE)
BUILD_ASSERT(CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE % MIN_CLUSTER_SIZE == 0,
	     "File size must be multiple of minimum cluster size");

/** Generate filename for Infuse binary container */
#define GEN_FILENAME(buffer, config, infuse_id, index)                                             \
	snprintf(buffer, sizeof(buffer), "%s:infuse_%016llx_%06d.bin", config->disk, infuse_id,    \
		 index)

LOG_MODULE_REGISTER(data_logger_exfat, CONFIG_DATA_LOGGER_EXFAT_LOG_LEVEL);

static uint32_t disk_lba_from_block(const struct device *dev, uint32_t phy_block)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t file_num = phy_block / BLOCKS_PER_FILE;
	uint32_t file_offset = phy_block % BLOCKS_PER_FILE;
	uint64_t dev_id = infuse_device_id();
	char filename[40];
	FRESULT res;
	FIL fp;

	/* Have the right file offset cached */
	if (file_num == data->cached_file_num) {
		return data->cached_file_lba + file_offset;
	}

	/* Create filename string */
	GEN_FILENAME(filename, config, dev_id, file_num);

	/* Get file info */
	res = f_open(&fp, filename, FA_READ);
	if (res != FR_OK) {
		/* File does not exist */
		return LBA_NO_FILE;
	}

	/* Validate file was created correctly */
	if (f_size(&fp) == 0) {
		return LBA_NO_MEM;
	}

	/* Get physical location of the file data:
	 *   http://elm-chan.org/fsw/ff/doc/expand.html
	 */
	data->cached_file_num = file_num;
	data->cached_file_lba = fp.obj.fs->database + fp.obj.fs->csize * (fp.obj.sclust - 2);

	(void)f_close(&fp);
	return data->cached_file_lba + file_offset;
}

static int binary_container_create(const struct device *dev, uint32_t phy_block)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t file_num = phy_block / BLOCKS_PER_FILE;
	uint32_t fsize = CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE;
	uint64_t dev_id = infuse_device_id();
	uint32_t start_lba;
	char filename[40];
	bool del = false;
	FRESULT res;
	FIL fp;

	/* Create filename string */
	GEN_FILENAME(filename, config, dev_id, file_num);

	LOG_INF("Creating %s", filename);
	res = f_open(&fp, filename, FA_CREATE_NEW | FA_WRITE);
	if (res != FR_OK) {
		LOG_ERR("f_open failed: %d %s", res, filename);
		return -EBADF;
	}
	res = f_expand(&fp, fsize, 1);
	if (res != FR_OK) {
		if (res == FR_DENIED) {
			LOG_WRN("Disk full at %d/%d blocks", data->common.current_block,
				data->common.physical_blocks);
			data->common.logical_blocks = data->common.current_block;
			data->common.physical_blocks = data->common.current_block;
			/* Delete the file so init doesn't think data exists on the empty file */
			del = true;
		} else {
			LOG_ERR("f_expand failed: %d", res);
		}
		res = -ENOMEM;
	}
	(void)f_close(&fp);

	if (del) {
		LOG_INF("Deleting %s", filename);
		f_unlink(filename);
	}

	/* Reset entire file to erased state */
	if (res == FR_OK) {
		start_lba = disk_lba_from_block(dev, phy_block);
		res = disk_access_erase(config->disk, start_lba, BLOCKS_PER_FILE);
	}
	return res;
}

int logger_exfat_file_next(const struct device *dev)
{
	struct dl_exfat_data *data = dev->data;
	uint32_t in_current_block;
	uint32_t to_skip;
	uint32_t disk_lba;
	int rc = 0;

	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);

	in_current_block = data->common.current_block % BLOCKS_PER_FILE;
	if (in_current_block) {
		to_skip = (BLOCKS_PER_FILE - in_current_block);
		data->common.current_block += to_skip;
		LOG_WRN("Skipped %d blocks", to_skip);
	}
	if (data->common.current_block % BLOCKS_PER_FILE == 0) {
		disk_lba = disk_lba_from_block(dev, data->common.current_block);
		if (disk_lba == LBA_NO_MEM) {
			rc = -ENOMEM;
		} else if (disk_lba == LBA_NO_FILE) {
			rc = binary_container_create(dev, data->common.current_block);
		}
	}
	logger_exfat_filesystem_release(dev);
	return rc;
}

static int logger_exfat_write_burst(const struct device *dev, uint32_t start_block,
				    uint32_t num_blocks, const void *block_data)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t disk_lba, disk_lba_end;
	uint32_t write_iter;
	int rc;

	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);
restart:
	disk_lba = disk_lba_from_block(dev, start_block);

	/* No memory left on filesystem */
	if (disk_lba == LBA_NO_MEM) {
		rc = -ENOMEM;
		goto end;
	}
	/* File does not exist on filesystem */
	else if (disk_lba == LBA_NO_FILE) {
		/* Allocate the binary file on the filesystem */
		rc = binary_container_create(dev, start_block);
		if (rc < 0) {
			goto end;
		}
		/* Re-evaluate */
		goto restart;
	}

	/* Number of empty blocks remaining on the current file */
	disk_lba_end = data->cached_file_lba + BLOCKS_PER_FILE;
	/* How many blocks to write this iteration */
	write_iter = MIN(num_blocks, (disk_lba_end - disk_lba));

	LOG_DBG("Writing to logger block: %08X (%d) LBA: %08X", start_block, write_iter, disk_lba);
	rc = disk_access_write(config->disk, block_data, disk_lba, write_iter);
	if (rc == 0) {
		/* Sync on each write for now */
		rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_SYNC, NULL);
	}
	num_blocks -= write_iter;
	if (num_blocks) {
		/* Didn't write the entire block, loop again */
		LOG_DBG("Looping for remaining %d", num_blocks);
		start_block += write_iter;
		block_data = (uint8_t *)block_data + (DATA_LOGGER_EXFAT_BLOCK_SIZE * write_iter);
		goto restart;
	}

end:
	logger_exfat_filesystem_release(dev);
	return rc;
}

static int logger_exfat_write(const struct device *dev, uint32_t phy_block,
			      enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	__ASSERT(mem_len == DATA_LOGGER_EXFAT_BLOCK_SIZE, "Not full block");

	return logger_exfat_write_burst(dev, phy_block, 1, mem);
}

static int logger_exfat_read(const struct device *dev, uint32_t phy_block, uint16_t block_offset,
			     void *mem, uint16_t mem_len)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	bool aligned = ((uintptr_t)mem % sizeof(uint32_t)) == 0;
	uint32_t disk_lba;
	int rc;

	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);
	disk_lba = disk_lba_from_block(dev, phy_block);

	if ((disk_lba == LBA_NO_FILE) || (disk_lba == LBA_NO_MEM)) {
		logger_exfat_filesystem_release(dev);
		/* File does not exist on filesystem, set data to 0xFF and return */
		memset(mem, 0xFF, mem_len);
		return 0;
	}

	LOG_DBG("Reading from logger block: %08X LBA: %08X", phy_block, disk_lba);

	if (aligned && (block_offset == 0) && (mem_len == DATA_LOGGER_EXFAT_BLOCK_SIZE)) {
		/* Read directly into provided buffer */
		rc = disk_access_read(config->disk, mem, disk_lba, 1);
	} else {
		/* Read complete block from file to device buffer */
		rc = disk_access_read(config->disk, data->block_buffer, disk_lba, 1);
		/* Memcpy required data out */
		memcpy(mem, data->block_buffer + block_offset, mem_len);
	}
	logger_exfat_filesystem_release(dev);
	return rc;
}

static int filesystem_init(const struct device *dev, const char *label)
{
	/* Common filesystem init */
	return logger_exfat_filesystem_common_init(dev, label);
}

static int logger_exfat_reset(const struct device *dev, uint32_t block_hint,
			      void (*erase_progress)(uint32_t blocks_erased))
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	const char *const infuse_prefix = "infuse_";
	uint32_t blocks_erased = 0;
	char filename[40];
	FILINFO fno;
	FRESULT fr;
	DIR dir;
	int rc = 0;

	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);

	snprintf(filename, sizeof(filename), "%s:", config->disk);

	/* Open the directory */
	fr = f_opendir(&dir, filename);
	if (fr != FR_OK) {
		rc = -EIO;
		goto release;
	}
	/* Iterate over each file in the directory */
	while (1) {
		fr = f_readdir(&dir, &fno);
		if (fr != FR_OK || fno.fname[0] == 0) {
			/* Error or end of directory */
			break;
		}

		if (fno.fattrib & AM_DIR) {
			/* Directory, not file */
			continue;
		}

		if (strncmp(infuse_prefix, fno.fname, strlen(infuse_prefix)) != 0) {
			/* Not an Infuse binary data file */
			continue;
		}

		/* Unlink the file. This doesn't erase any data, but file creation handles
		 * ensuring the file is in the right state once it is created again.
		 */
		LOG_DBG("Unlinking %s", fno.fname);
		fr = f_unlink(fno.fname);
		__ASSERT_NO_MSG(fr == FR_OK);

		/* Run user callback */
		blocks_erased += BLOCKS_PER_FILE;
		erase_progress(blocks_erased);
	}

	f_closedir(&dir);

release:
	logger_exfat_filesystem_release(dev);

	/* Reset cached values */
	data->cached_file_num = UINT32_MAX;
	data->cached_file_lba = UINT32_MAX;

	return rc;
}

#ifdef CONFIG_PM_DEVICE
static int exfat_multi_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct dl_exfat_config *config = dev->config;
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_DEINIT, NULL);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_INIT, NULL);
		break;
	default:
		return -ENOTSUP;
	}
	return rc;
}
#endif /* CONFIG_PM_DEVICE */

static int logger_exfat_range_hint(const struct device *dev, uint32_t *block_start,
				   uint32_t *block_end)
{
	const struct dl_exfat_config *config = dev->config;
	int last_file_idx = 0;
	char filename[40];
	char dir[20];
	FILINFO fno;
	FRESULT fr;
	DIR dj;

	snprintf(dir, sizeof(dir), "%s:", config->disk);
	snprintf(filename, sizeof(filename), "infuse_%016llx_??????.bin", infuse_device_id());

	fr = f_findfirst(&dj, &fno, dir, filename);
	while (fr == FR_OK && fno.fname[0]) {
		size_t fname_len = strlen(fno.fname);
		char *fname_idx_ptr = fno.fname + fname_len - 10;
		int fname_idx = atoi(fname_idx_ptr);

		/* Only consider files that match the expected size */
		if (fno.fsize == CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE) {
			last_file_idx = MAX(last_file_idx, fname_idx);
		}

		/* Next item */
		fr = f_findnext(&dj, &fno);
	}
	f_closedir(&dj);

	*block_start = last_file_idx * BLOCKS_PER_FILE;
	*block_end = (1 + last_file_idx) * BLOCKS_PER_FILE - 1;
	if (last_file_idx > 0) {
		/* There could be no data in the current file */
		*block_start -= 1;
	}

	LOG_DBG("Search range hint: %d-%d", *block_start, *block_end);
	return 0;
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int logger_exfat_init(const struct device *dev)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	bool infuse_fs = true;
	char disk_path[16];
	FRESULT res;
	int rc;

	data->cached_file_num = UINT32_MAX;
	data->cached_file_lba = UINT32_MAX;

	k_sem_init(&data->filesystem_claim, 1, 1);

	/* Initial mount attempt */
	snprintf(disk_path, sizeof(disk_path), "%s:", config->disk);
	res = f_mount(&data->infuse_fatfs, disk_path, 1);
	LOG_DBG("First mount: %d", res);
	if (res == FR_OK) {
		infuse_fs = logger_exfat_filesystem_is_infuse(dev, FILESYSTEM_LABEL);
	} else if (res == FR_NOT_READY) {
		LOG_WRN("Disk '%s' not ready", config->disk);
		return -EIO;
	}
	/* Handle standard mount failures */
	if ((res == FR_NO_FILESYSTEM) || (!infuse_fs)) {
		LOG_INF("Initialising disk '%s'", config->disk);
		res = filesystem_init(dev, FILESYSTEM_LABEL);
	}
	/* Handle errors */
	if (res != FR_OK) {
		LOG_ERR("Unknown mount problem (%d)", res);
		return -EIO;
	}

#if CONFIG_DATA_LOGGER_EXFAT_LOG_LEVEL >= LOG_LEVEL_DBG
	FILINFO fno;
	DIR dj;

	/* Search for number of files currently saved */
	res = f_findfirst(&dj, &fno, disk_path, "infuse_*.bin");
	while (res == FR_OK && fno.fname[0]) {
		LOG_DBG("Found: %s (%llu bytes)", fno.fname, fno.fsize);
		res = f_findnext(&dj, &fno);
	}
	f_closedir(&dj);
#endif

	/* Store disk info */
	logger_exfat_disk_info_store(dev);

	/* Setup common data structure */
	disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_COUNT, &data->common.physical_blocks);
	data->common.logical_blocks = data->common.physical_blocks;
	data->common.block_size = DATA_LOGGER_EXFAT_BLOCK_SIZE;
	data->common.erase_size = DATA_LOGGER_EXFAT_BLOCK_SIZE;
	data->common.erase_val = 0xFF;

	/* Filesystem is mounted */
	rc = data_logger_common_init(dev);

	if (!IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		/* Return without de-initialising the device */
		return rc;
	}

	/* Allow the backing device to power off */
	(void)disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_DEINIT, NULL);

	/* Always want PM enabled on this device */
	pm_device_init_suspended(dev);
	(void)pm_device_runtime_enable(dev);
	return rc;
}

const struct data_logger_api data_logger_exfat_api = {
	.write = logger_exfat_write,
#ifdef CONFIG_DATA_LOGGER_BURST_WRITES
	.write_burst = logger_exfat_write_burst,
#endif /* CONFIG_DATA_LOGGER_BURST_WRITES */
	.read = logger_exfat_read,
	.reset = logger_exfat_reset,
	.search_hint = logger_exfat_range_hint,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_exfat_config config##inst = {                                             \
		.common = COMMON_CONFIG_INIT(inst, true, false, 1),                                \
		.disk = DT_PROP(DT_INST_PROP(inst, disk), disk_name),                              \
	};                                                                                         \
	static struct dl_exfat_data data##inst;                                                    \
	PM_DEVICE_DT_INST_DEFINE(inst, exfat_multi_pm_control);                                    \
	DEVICE_DT_INST_DEFINE(inst, logger_exfat_init, PM_DEVICE_DT_INST_GET(inst), &data##inst,   \
			      &config##inst, POST_KERNEL, 80, &data_logger_exfat_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
