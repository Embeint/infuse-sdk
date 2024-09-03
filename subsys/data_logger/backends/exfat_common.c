/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include <infuse/time/epoch.h>

#include <ff.h>

#include "exfat_common.h"

static const char readme_text[] = "Infuse-IoT binary data logs\n";

LOG_MODULE_DECLARE(data_logger_exfat);

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

bool logger_exfat_filesystem_is_infuse(const struct device *dev)
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

int logger_exfat_filesystem_common_init(const struct device *dev)
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

	return res == FR_OK ? 0 : -EIO;
}
