/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include <ff.h>

#include "exfat_common.h"

static const char readme_text[] = "Infuse-IoT binary data logs\n";

LOG_MODULE_DECLARE(data_logger_exfat, CONFIG_DATA_LOGGER_EXFAT_LOG_LEVEL);

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

bool logger_exfat_filesystem_is_infuse(const struct device *dev, const char *label)
{
	const struct dl_exfat_config *config = dev->config;
	char fs_label[7] = {0};
	char path[32];
	FRESULT res;
	FIL fp;

	snprintf(path, sizeof(path), "%s:", config->disk);

	res = f_getlabel(path, fs_label, NULL);
	if (res != FR_OK || (strncmp(fs_label, label, 7) != 0)) {
		LOG_ERR("Bad filesystem label '%s'", fs_label);
		return false;
	}

	snprintf(path, sizeof(path), "%s:DELETE_TO_RESET.txt", config->disk);
	res = f_open(&fp, path, FA_READ);
	if (res != FR_OK) {
		/* File does not exist, reset */
		return false;
	}
	(void)f_close(&fp);
	return true;
}

int logger_exfat_filesystem_common_init(const struct device *dev, const char *label)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;
	const MKFS_PARM mkfs_opt = {
		.fmt = FM_EXFAT,
#ifdef CONFIG_DISK_DRIVER_SDMMC
		/* We know our filesystem only hosts large block files, so for
		 * SD cards use the largest recommended cluster size (128kB).
		 */
		.au_size = 128 * 1024,
#endif /* CONFIG_DISK_DRIVER_SDMMC */
	};
	char path[32];
	uint32_t blocks;
	FRESULT res;
	UINT bw;
	FIL fp;

	snprintf(path, sizeof(path), "%s:", config->disk);

	/* Pre-erase the disk */
	disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_COUNT, &blocks);
	res = disk_access_erase(config->disk, 0, blocks);
	if (res != FR_OK) {
		LOG_ERR("disk_access_erase failed: %d", res);
		return -EIO;
	}

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
	snprintf(path, sizeof(path), "%s:%s", config->disk, label);
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

	/* Create static DELETE_TO_RESET.txt */
	snprintf(path, sizeof(path), "%s:DELETE_TO_RESET.txt", config->disk);
	res = f_open(&fp, path, FA_CREATE_NEW | FA_WRITE);
	if (res != FR_OK) {
		LOG_ERR("f_open failed: %d", res);
		return -EIO;
	}
	(void)f_close(&fp);

	return res == FR_OK ? 0 : -EIO;
}

void logger_exfat_disk_info_store(const struct device *dev)
{
#ifdef CONFIG_KV_STORE_KEY_EXFAT_DISK_INFO
	const struct dl_exfat_config *config = dev->config;
	struct kv_exfat_disk_info disk_info;
	uint32_t block_count, block_size;

	/* Get disk info */
	disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
	disk_access_ioctl(config->disk, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);

	disk_info.block_count = block_count;
	disk_info.block_size = block_size;

	(void)KV_STORE_WRITE(KV_KEY_EXFAT_DISK_INFO, &disk_info);
#endif /* CONFIG_KV_STORE_KEY_EXFAT_DISK_INFO */
}

const char *logger_exfat_filesystem_claim(const struct device *dev, uint8_t **buf, size_t *buf_size,
					  k_timeout_t timeout)
{
	const struct dl_exfat_config *config = dev->config;
	struct dl_exfat_data *data = dev->data;

	if (k_sem_take(&data->filesystem_claim, timeout) != 0) {
		return NULL;
	}
	if (buf != NULL) {
		__ASSERT_NO_MSG(buf_size != NULL);
		*buf = data->block_buffer;
		*buf_size = sizeof(data->block_buffer);
	}
	return config->disk;
}

void logger_exfat_filesystem_release(const struct device *dev)
{
	struct dl_exfat_data *data = dev->data;

	k_sem_give(&data->filesystem_claim);
}
