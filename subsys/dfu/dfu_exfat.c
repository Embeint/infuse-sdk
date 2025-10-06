/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>

#include <infuse/version.h>
#include <infuse/data_logger/backend/exfat.h>
#include <infuse/dfu/exfat.h>
#include <infuse/dfu/helpers.h>

#include <ff.h>

LOG_MODULE_REGISTER(dfu_exfat, LOG_LEVEL_INF);

static int highest_version_on_path(const char *path, struct infuse_version *app_current,
				   struct infuse_version *app_upgrade)
{
	struct infuse_version file_version;
	int cnt, rc = 0;
	FILINFO fno;
	FRESULT res;
	DIR dir;

	/* Open the directory */
	res = f_opendir(&dir, path);
	if (res == FR_NO_PATH) {
		return 0;
	} else if (res != FR_OK) {
		return -ENOENT;
	}
	while (1) {
		/* Read a directory item */
		res = f_readdir(&dir, &fno);
		if (res != FR_OK) {
			/* Error */
			rc = -EIO;
			break;
		}
		if (fno.fname[0] == 0) {
			/* End of iteration */
			break;
		}
		if (fno.fattrib & AM_DIR) {
			/* Directory */
			continue;
		} else {
			/* File, scan for version number */
			cnt = sscanf(fno.fname, "%" SCNu8 "_%" SCNu8 "_%" SCNu16 ".bin",
				     &file_version.major, &file_version.minor,
				     &file_version.revision);
			if (cnt != 3) {
				continue;
			}
			if (infuse_version_compare(app_current, &file_version) <= 0) {
				continue;
			}
			if (infuse_version_compare(app_upgrade, &file_version) <= 0) {
				continue;
			}
			*app_upgrade = file_version;
			rc = 1;
		}
	}
	f_closedir(&dir);
	return rc;
}

int dfu_exfat_app_upgrade_exists(const struct device *dev, struct infuse_version *upgrade)
{
	struct infuse_version app_current = application_version_get();
	const char *disk;
	char path[32];
	int rc;

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		return rc;
	}

	/* Claim the filesystem */
	disk = logger_exfat_filesystem_claim(dev, NULL, NULL, K_FOREVER);

	/* Check for application upgrades */
	snprintf(path, sizeof(path), "%s:dfu/app", disk);
	rc = highest_version_on_path(path, &app_current, upgrade);

	/* Release the filesystem */
	logger_exfat_filesystem_release(dev);
	/* Release device after a delay */
	(void)pm_device_runtime_put_async(dev, K_MSEC(100));
	return rc;
}

int dfu_exfat_app_upgrade_copy(const struct device *dev, struct infuse_version upgrade,
			       uint8_t flash_area_id, dfu_exfat_progress_cb_t progress_cb)
{
	const struct flash_area *fa;
	uint8_t *block_buffer;
	size_t block_size;
	size_t remaining;
	off_t offset;
	const char *disk;
	char path[32];
	int rc = 0;
	FILINFO fno;
	FIL fp;

	/* Request backend to be powered */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		return rc;
	}

	/* Claim the filesystem */
	disk = logger_exfat_filesystem_claim(dev, &block_buffer, &block_size, K_FOREVER);

	/* Expected path of the upgrade file */
	snprintf(path, sizeof(path), "%s:dfu/app/%d_%d_%d.bin", disk, upgrade.major, upgrade.minor,
		 upgrade.revision);

	/* Get upgrade file information */
	if (f_stat(path, &fno) != FR_OK) {
		rc = -ENOENT;
		goto end;
	}

	/* Open input file */
	if (f_open(&fp, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
		rc = -EIO;
		goto end;
	}

	/* Open output flash area */
	if (flash_area_open(flash_area_id, &fa) != 0) {
		rc = -EIO;
		goto close_file;
	}

	/* Erase output area */
	if (infuse_dfu_image_erase(fa, fno.fsize, NULL, true) != 0) {
		rc = -EIO;
		goto close_area;
	}

	/* Copy data from filesystem to flash area */
	offset = 0;
	remaining = fno.fsize;
	while (remaining) {
		size_t iter = MIN(remaining, block_size);
		UINT read;

		/* Read next chunk from file */
		if (f_read(&fp, block_buffer, iter, &read) != FR_OK) {
			rc = -EIO;
			break;
		}

		/* Write data to output area */
		if (flash_area_write(fa, offset, block_buffer, iter) != 0) {
			rc = -EIO;
			break;
		}
		offset += iter;
		remaining -= iter;

		/* Call progress callback if supplied */
		if (progress_cb) {
			progress_cb(offset, fno.fsize);
		}
	}

close_area:
	flash_area_close(fa);
close_file:
	f_close(&fp);
end:
	/* Release the filesystem */
	logger_exfat_filesystem_release(dev);
	/* Release device after a delay */
	(void)pm_device_runtime_put_async(dev, K_MSEC(100));
	return rc;
}
