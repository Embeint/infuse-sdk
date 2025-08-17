/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#define DT_DRV_COMPAT embeint_data_logger_exfat

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/exfat.h>
#include <infuse/identifiers.h>

#include <ff.h>

#include "common.h"
#include "exfat_common.h"

#define FILESYSTEM_LABEL "INFUSE-SF"

LOG_MODULE_REGISTER(data_logger_exfat, CONFIG_DATA_LOGGER_EXFAT_LOG_LEVEL);

static int logger_exfat_write_burst(const struct device *dev, uint32_t start_block,
				    uint32_t num_blocks, const void *block_data)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint32_t disk_lba = data->cached_file_lba + start_block;
	int rc;

	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);

	LOG_DBG("Writing %d blocks: %08X LBA: %08X", num_blocks, start_block, disk_lba);
	rc = disk_access_write(config->disk, block_data, disk_lba, num_blocks);
	if (rc == 0) {
		/* Sync on each write for now */
		rc = disk_access_ioctl(config->disk, DISK_IOCTL_CTRL_SYNC, NULL);
	}
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
	uint32_t disk_lba = data->cached_file_lba + phy_block;
	int rc;

	LOG_DBG("Reading from logger block: %08X LBA: %08X", phy_block, disk_lba);

	if (aligned && (block_offset == 0) && (mem_len == DATA_LOGGER_EXFAT_BLOCK_SIZE)) {
		/* Read directly into provided buffer */
		rc = disk_access_read(config->disk, mem, disk_lba, 1);
	} else {
		/* Read complete block from file */
		rc = disk_access_read(config->disk, data->block_buffer, disk_lba, 1);
		/* Memcpy required data out */
		memcpy(mem, data->block_buffer + block_offset, mem_len);
	}
	return rc;
}

static int logger_exfat_reset(const struct device *dev, uint32_t block_hint,
			      void (*erase_progress)(uint32_t blocks_erased))
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	int rc;

	ARG_UNUSED(block_hint);
	ARG_UNUSED(erase_progress);
	/* For SD cards, erase duration is independent of the amount to erase.
	 * Therefore the best thing to do is simply erase the file in a single chunk.
	 * If the exFAT logger is used with a flash chip, this is not true.
	 */
	(void)logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);
	rc = disk_access_erase(config->disk, data->cached_file_lba, data->common.physical_blocks);
	logger_exfat_filesystem_release(dev);
	return rc;
}

static int filesystem_init(const struct device *dev, const char *label, const char *bin_file)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	uint64_t fsize;
	char path[40];
	DWORD fre_clust, fre_sect;
	FRESULT res;
	FATFS *fs;
	FIL fp;

	/* Common filesystem init */
	if (logger_exfat_filesystem_common_init(dev, label) < 0) {
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

#ifdef CONFIG_PM_DEVICE
static int exfat_single_pm_control(const struct device *dev, enum pm_device_action action)
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

int logger_exfat_file_next(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* There is only a single file, no file creation overhead */
	return 0;
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
	int rc;

	k_sem_init(&data->filesystem_claim, 1, 1);

	/* Initial mount attempt */
	snprintf(path, sizeof(path), "%s:", config->disk);
	res = f_mount(&data->infuse_fatfs, path, 1);
	LOG_DBG("First mount: %d", res);
	if (res == FR_OK) {
		infuse_fs = logger_exfat_filesystem_is_infuse(dev, FILESYSTEM_LABEL);
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
		res = filesystem_init(dev, FILESYSTEM_LABEL, path);
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

	/* Store disk info */
	logger_exfat_disk_info_store(dev);

	/* Setup common data structure */
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
};

#define DATA_LOGGER_DEFINE(inst)                                                                   \
	COMMON_CONFIG_PRE(inst);                                                                   \
	static struct dl_exfat_config config##inst = {                                             \
		.common = COMMON_CONFIG_INIT(inst, true, false, 1),                                \
		.disk = DT_PROP(DT_INST_PROP(inst, disk), disk_name),                              \
	};                                                                                         \
	static struct dl_exfat_data data##inst;                                                    \
	PM_DEVICE_DT_INST_DEFINE(inst, exfat_single_pm_control);                                   \
	DEVICE_DT_INST_DEFINE(inst, logger_exfat_init, PM_DEVICE_DT_INST_GET(inst), &data##inst,   \
			      &config##inst, POST_KERNEL, 80, &data_logger_exfat_api);

#define DATA_LOGGER_DEFINE_WRAPPER(inst)                                                           \
	IF_ENABLED(DATA_LOGGER_DEPENDENCIES_MET(DT_DRV_INST(inst)), (DATA_LOGGER_DEFINE(inst)))

DT_INST_FOREACH_STATUS_OKAY(DATA_LOGGER_DEFINE_WRAPPER)
