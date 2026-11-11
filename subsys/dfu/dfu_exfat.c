/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/version.h>
#include <infuse/data_logger/backend/exfat.h>
#include <infuse/dfu/exfat.h>
#include <infuse/dfu/helpers.h>

#include <ff.h>

/* MCUboot image header (version 1) magic number */
#define MCUBOOT_HEADER_MAGIC 0x96f3b83d
#define MCUBOOT_HEADER_SIZE  32

/* Leading fields of the MCUboot image header */
struct mcuboot_raw_header {
	uint32_t magic;
	uint32_t load_address;
	uint16_t header_size;
	uint16_t protect_tlv_size;
	uint32_t image_size;
	uint32_t flags;
	uint8_t major;
	uint8_t minor;
	uint16_t revision;
	uint32_t build_num;
} __packed;

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

	/* Only versions newer than the current application are upgrades */
	*upgrade = app_current;

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

/* Validate the MCUboot header at the start of the file against the expected version */
static int file_header_validate(FIL *fp, FSIZE_t fsize, struct infuse_version *upgrade,
				struct infuse_version *header_version)
{
	struct mcuboot_raw_header header;
	UINT read;

	if ((f_read(fp, &header, sizeof(header), &read) != FR_OK) || (read != sizeof(header)) ||
	    (f_lseek(fp, 0) != FR_OK)) {
		return -EIO;
	}
	if ((sys_le32_to_cpu(header.magic) != MCUBOOT_HEADER_MAGIC) ||
	    (sys_le16_to_cpu(header.header_size) < MCUBOOT_HEADER_SIZE) ||
	    ((uint64_t)sys_le16_to_cpu(header.header_size) + sys_le32_to_cpu(header.image_size) >
	     fsize)) {
		LOG_ERR("Invalid image header");
		return -EINVAL;
	}
	*header_version = (struct infuse_version){
		.major = header.major,
		.minor = header.minor,
		.revision = sys_le16_to_cpu(header.revision),
		.build_num = sys_le32_to_cpu(header.build_num),
	};
	if (infuse_version_compare(upgrade, header_version) != 0) {
		LOG_ERR("Image version %d.%d.%d does not match file name", header_version->major,
			header_version->minor, header_version->revision);
		return -EINVAL;
	}
	return 0;
}

/* Check whether the output slot already holds this exact image */
static bool image_already_present(uint8_t flash_area_id, const struct infuse_version *version)
{
	struct mcuboot_img_header slot;

	if ((boot_read_bank_header(flash_area_id, &slot, sizeof(slot)) != 0) ||
	    (slot.mcuboot_version != 1)) {
		return false;
	}
	return (slot.h.v1.sem_ver.major == version->major) &&
	       (slot.h.v1.sem_ver.minor == version->minor) &&
	       (slot.h.v1.sem_ver.revision == version->revision) &&
	       (slot.h.v1.sem_ver.build_num == version->build_num);
}

int dfu_exfat_app_upgrade_copy(const struct device *dev, struct infuse_version upgrade,
			       uint8_t flash_area_id, infuse_progress_cb_t erase_progress_cb,
			       infuse_progress_cb_t write_progress_cb)
{
	struct infuse_version header_version;
	const struct flash_area *fa;
	uint8_t *block_buffer;
	size_t block_size;
	size_t image_offset;
	size_t image_size;
	size_t remaining;
	size_t write_align;
	size_t offset;
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

	/* File must contain an image of the version in its name */
	rc = file_header_validate(&fp, fno.fsize, &upgrade, &header_version);
	if (rc < 0) {
		goto close_file;
	}

	/* An image that is already present was copied previously, and has been reverted, or
	 * has not been requested. Copying and requesting it again could loop forever.
	 */
	if (image_already_present(flash_area_id, &header_version)) {
		LOG_WRN("Image already present in output area");
		rc = -EALREADY;
		goto close_file;
	}

	/* Open output flash area */
	if (flash_area_open(flash_area_id, &fa) != 0) {
		rc = -EIO;
		goto close_file;
	}

	/* Image may not start at the beginning of the area (swap-using-offset) */
	image_offset = boot_get_image_start_offset(flash_area_id);
	write_align = flash_area_align(fa);

	/* Image must fit in the area, final chunk is padded to the write alignment */
	if ((image_offset > fa->fa_size) || (fno.fsize > fa->fa_size - image_offset) ||
	    (block_size % write_align != 0)) {
		rc = -EFBIG;
		goto close_area;
	}
	image_size = fno.fsize;

	/* Erase output area */
	if (infuse_dfu_image_erase(fa, image_offset + image_size, erase_progress_cb, true) != 0) {
		rc = -EIO;
		goto close_area;
	}

	/* Copy data from filesystem to flash area */
	infuse_dfu_write_erase_start(fa);
	offset = 0;
	remaining = image_size;
	while (remaining) {
		size_t iter = MIN(remaining, block_size);
		size_t write_len = ROUND_UP(iter, write_align);
		UINT read;

		/* Read next chunk from file */
		if ((f_read(&fp, block_buffer, iter, &read) != FR_OK) || (read != iter)) {
			rc = -EIO;
			break;
		}
		/* Pad the final chunk to the write alignment */
		memset(block_buffer + iter, flash_area_erased_val(fa), write_len - iter);

		/* Write data to output area */
		if (flash_area_write(fa, image_offset + offset, block_buffer, write_len) != 0) {
			rc = -EIO;
			break;
		}
		offset += iter;
		remaining -= iter;

		/* Call progress callback if supplied */
		if (write_progress_cb) {
			write_progress_cb(offset, image_size);
		}
	}
	infuse_dfu_write_erase_finish(fa);

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
