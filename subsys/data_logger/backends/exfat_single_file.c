/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#define DT_DRV_COMPAT embeint_data_logger_exfat

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include <infuse/data_logger/logger.h>
#include <infuse/time/epoch.h>
#include <infuse/identifiers.h>

#include <ff.h>

#include "common.h"

#define DATA_LOGGER_EXFAT_BLOCK_SIZE 512

#define LBA_NO_FILE      (UINT32_MAX)
#define LBA_NO_MEM       (UINT32_MAX - 1)
#define MIN_CLUSTER_SIZE 4096

#define BLOCKS_PER_FILE (CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE / DATA_LOGGER_EXFAT_BLOCK_SIZE)
BUILD_ASSERT(CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE % MIN_CLUSTER_SIZE == 0,
	     "File size must be multiple of minimum cluster size");

struct dl_exfat_config {
	struct data_logger_common_config common;
	const char *disk;
};
struct dl_exfat_data {
	struct data_logger_common_data common;
	FATFS infuse_fatfs;
	uint8_t block_buffer[DATA_LOGGER_EXFAT_BLOCK_SIZE];
	uint32_t cached_file_lba;
};

static const char readme_text[] = "Infuse-IoT binary data logs\n";

LOG_MODULE_REGISTER(data_logger_exfat, CONFIG_DATA_LOGGER_EXFAT_LOG_LEVEL);

DWORD get_fattime(void)
{
	time_t unix_time = unix_time_from_epoch(epoch_time_now());
	struct tm *cal;

	/* Convert to calendar time */
	cal = localtime(&unix_time);

	/* From http://elm-chan.org/fsw/ff/doc/fattime.html */
	return (DWORD)(cal->tm_year - 80) << 25 | (DWORD)(cal->tm_mon + 1) << 21 |
	       (DWORD)cal->tm_mday << 16 | (DWORD)cal->tm_hour << 11 | (DWORD)cal->tm_min << 5 |
	       (DWORD)cal->tm_sec >> 1;
}

static bool filesystem_is_infuse(const struct device *dev)
{
	const struct dl_exfat_config *config = dev->config;
	char label[34] = {0};
	char disk_path[16];
	FRESULT res;

	snprintf(disk_path, sizeof(disk_path), "%s:", config->disk);

	res = f_getlabel(disk_path, label, NULL);
	if (res != FR_OK || (strncmp(label, "INFUSE", 7) != 0)) {
		LOG_ERR("Bad filesystem label '%s'", label);
		return false;
	}
	return true;
}

static int logger_exfat_write(const struct device *dev, uint32_t phy_block,
			      enum infuse_type data_type, const void *mem, uint16_t mem_len)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t disk_lba = data->cached_file_lba + phy_block;
	int rc;

	__ASSERT(mem_len == DATA_LOGGER_EXFAT_BLOCK_SIZE, "Not full block");

	LOG_DBG("Writing to logger block: %08X LBA: %08X", phy_block, disk_lba);
	rc = disk_access_write(config->disk, mem, disk_lba, 1);
	if (rc == 0) {
		/* Sync on each write for now */
		rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_SYNC, NULL);
	}
	return rc;
}

static int logger_exfat_read(const struct device *dev, uint32_t phy_block, uint16_t block_offset,
			     void *mem, uint16_t mem_len)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t disk_lba = data->cached_file_lba + phy_block;
	int rc;

	LOG_DBG("Reading from logger block: %08X LBA: %08X", phy_block, disk_lba);

	/* Read complete block from file */
	rc = disk_access_read(config->disk, data->block_buffer, disk_lba, 1);
	/* Memcpy required data out */
	memcpy(mem, data->block_buffer + block_offset, mem_len);
	return rc;
}

static int filesystem_init(const struct device *dev, const char *bin_file)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint64_t fsize;
	char path[40];

	const MKFS_PARM mkfs_opt = {
		.fmt = FM_EXFAT,
#ifdef CONFIG_DISK_DRIVER_SDMMC
		/* We know our filesystem only hosts large block files, so for
		 * SD cards use the largest recommended cluster size (128kB).
		 */
		.au_size = 128 * 1024,
#endif /* CONFIG_DISK_DRIVER_SDMMC */
	};
	DWORD fre_clust, fre_sect;
	FRESULT res;
	FATFS *fs;
	UINT bw;
	FIL fp;

	snprintf(path, sizeof(path), "%s:", config->disk);

	/* Create the filesystem */
	res = f_mkfs(path, &mkfs_opt, data->block_buffer, sizeof(data->block_buffer));
	if (res != FR_OK) {
		LOG_ERR("f_mkfs failed: %d", res);
		return -EIO;
	}

	/* Mount the filesystem */
	res = f_mount(&data->infuse_fatfs, path, 1);
	if (res != FR_OK) {
		LOG_ERR("f_mount failed after f_mkfs: %d", res);
		return -EIO;
	}

	/* Set label ID */
	snprintf(path, sizeof(path), "%s:INFUSE", config->disk);
	res = f_setlabel(path);
	if (res != FR_OK) {
		LOG_ERR("f_setlabel failed: %d", res);
		return -EIO;
	}

	/* Create static README.txt file */
	snprintf(path, sizeof(path), "%s:README.txt", config->disk);
	res = f_open(&fp, path, FA_CREATE_NEW | FA_WRITE);
	if (res != FR_OK) {
		LOG_ERR("f_open failed: %d", res);
		return -EIO;
	}
	res = f_write(&fp, readme_text, sizeof(readme_text), &bw);
	if ((res != FR_OK) || (bw != sizeof(readme_text))) {
		LOG_ERR("f_write failed: %d (%d != %d)", res, bw, sizeof(readme_text));
	}
	(void)f_close(&fp);
	if (res != FR_OK) {
		return -EIO;
	}

	/* Find free space on the disk */
	snprintf(path, sizeof(path), "%s:", config->disk);
	f_getfree(path, &fre_clust, &fs);

	fre_sect = fre_clust * fs->csize;
	fsize = DATA_LOGGER_EXFAT_BLOCK_SIZE * (uint64_t)fre_sect;

	/* Create binary data container */
	LOG_DBG("Creating %s", bin_file);
	res = f_open(&fp, bin_file, FA_CREATE_NEW | FA_WRITE);
	if (res != FR_OK) {
		LOG_ERR("f_open failed: %d %s", res, bin_file);
		return -EBADF;
	}
	res = f_expand(&fp, fsize, 1);
	if (res != FR_OK) {
		LOG_ERR("f_expand failed: %d %llx", res, fsize);
	}

	/* Get start block address of file */
	data->cached_file_lba = fp.obj.fs->database + fp.obj.fs->csize * (fp.obj.sclust - 2);
	data->common.physical_blocks = fre_sect;

	/* Erase the contents of the binary file */
	if (res == FR_OK) {
		res = disk_access_erase(config->disk, data->cached_file_lba,
					data->common.physical_blocks);
		if (res != 0) {
			LOG_ERR("Failed to erase file: %d", res);
		}
	}

	/* Cleanup file */
	(void)f_close(&fp);
	return res;
}

/* Need to hook into this function when testing */
IF_DISABLED(CONFIG_ZTEST, (static))
int logger_exfat_init(const struct device *dev)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	bool infuse_fs = true;
	char path[40];
	FRESULT res;
	FILINFO fno;
	FIL fp;

	/* Initial mount attempt */
	snprintf(path, sizeof(path), "%s:", config->disk);
	res = f_mount(&data->infuse_fatfs, path, 1);
	LOG_DBG("First mount: %d", res);
	if (res == FR_OK) {
		infuse_fs = filesystem_is_infuse(dev);
	} else if (res == FR_NOT_READY) {
		LOG_WRN("Disk '%s' not ready", config->disk);
		return -EIO;
	}
	/* Format container file */
	snprintf(path, sizeof(path), "%s:infuse_%016llx_000000.bin", config->disk,
		 infuse_device_id());

	if ((res == FR_NO_FILESYSTEM) || (!infuse_fs)) {
		/* Handle standard mount failures */
		LOG_WRN("Creating filesystem on '%s'", config->disk);
		res = filesystem_init(dev, path);
	} else if (res == FR_OK) {
		/* Filesystem mounted, get file information */
		res = f_stat(path, &fno);
		if (res != FR_OK) {
			LOG_ERR("f_stat failed: %d %s", res, path);
			return -EBADF;
		}
		res = f_open(&fp, path, FA_READ);
		if (res != FR_OK) {
			LOG_ERR("f_open failed: %d %s", res, path);
			return -EBADF;
		}
		data->cached_file_lba =
			fp.obj.fs->database + fp.obj.fs->csize * (fp.obj.sclust - 2);
		data->common.physical_blocks = fno.fsize / DATA_LOGGER_EXFAT_BLOCK_SIZE;
		(void)f_close(&fp);
	} else {
		/* Handle errors */
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

	/* Setup common data structure */
	data->common.logical_blocks = data->common.physical_blocks;
	data->common.block_size = DATA_LOGGER_EXFAT_BLOCK_SIZE;
	data->common.erase_size = DATA_LOGGER_EXFAT_BLOCK_SIZE;
	data->common.erase_val = 0xFF;

	/* Filesystem is mounted */
	return data_logger_common_init(dev);
}

const struct data_logger_api data_logger_exfat_api = {
	.write = logger_exfat_write,
	.read = logger_exfat_read,
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_exfat_config config##inst = {                                             \
		.common = COMMON_CONFIG_INIT(inst, true, false),                                   \
		.disk = DT_PROP(DT_INST_PROP(inst, disk), disk_name),                              \
	};                                                                                         \
	static struct dl_exfat_data data##inst;                                                    \
	DEVICE_DT_INST_DEFINE(inst, logger_exfat_init, NULL, &data##inst, &config##inst,           \
			      POST_KERNEL, 80, &data_logger_exfat_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
