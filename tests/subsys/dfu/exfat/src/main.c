/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/random/random.h>

#include <infuse/data_logger/backend/exfat.h>
#include <infuse/dfu/exfat.h>
#include <infuse/version.h>

#define DISK_NAME DT_PROP(DT_PROP(DT_NODELABEL(data_logger_exfat), disk), disk_name)

static uint8_t input_buffer[2048] = {0};
static uint8_t output_buffer[2048];
static uint32_t sector_count;
static uint32_t sector_size;
static uint64_t device_id = 0x0123456789ABCDEF;

uint64_t vendor_infuse_device_id(void)
{
	return device_id;
}

static void flash_area_validate(uint8_t flash_area_id, uint8_t *expected, size_t expected_len)
{
	const struct flash_area *fa;

	zassert_equal(0, flash_area_open(flash_area_id, &fa));
	zassert_equal(0, flash_area_read(fa, 0, output_buffer, expected_len));
	zassert_mem_equal(expected, output_buffer, expected_len);

	flash_area_close(fa);
}

static void progress_cb_validate(size_t written, size_t total)
{
	static size_t tracked_written;

	tracked_written = MIN(tracked_written + 512, total);
	zassert_equal(tracked_written, written);
	zassert_equal(1569, total);
}

int logger_exfat_init(const struct device *dev);

ZTEST(dfu_exfat, test_dfu_image_find)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	int output_partition = FIXED_PARTITION_ID(slot1_partition);
	struct infuse_version upgrade_version;
	char disk_path[64];
	const char *disk;
	FILINFO info;
	UINT written;
	FIL fp;

	/* Initialise filesystem */
	zassert_equal(0, logger_exfat_init(logger));

	/* Run function without any folders */
	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT,
		      dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL));

	/* Create app image folder */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu", disk);
	zassert_equal(FR_OK, f_mkdir(disk_path));
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app", disk);
	zassert_equal(FR_OK, f_mkdir(disk_path));
	zassert_equal(FR_OK, f_stat(disk_path, &info));
	logger_exfat_filesystem_release(logger);

	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT,
		      dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL));

	/* Create upgrade file with smaller version number */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app/1_7_12.bin", disk);
	zassert_equal(FR_OK, f_open(&fp, disk_path, FA_CREATE_NEW | FA_WRITE));
	sys_rand_get(input_buffer, sizeof(input_buffer));
	zassert_equal(FR_OK, f_write(&fp, input_buffer, sizeof(input_buffer), &written));
	zassert_equal(FR_OK, f_close(&fp));
	logger_exfat_filesystem_release(logger);

	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT,
		      dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL));

	/* Create upgrade file with same version number */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app/2_1_4.bin", disk);
	zassert_equal(FR_OK, f_open(&fp, disk_path, FA_CREATE_NEW | FA_WRITE));
	sys_rand_get(input_buffer, sizeof(input_buffer));
	zassert_equal(FR_OK, f_write(&fp, input_buffer, sizeof(input_buffer), &written));
	zassert_equal(FR_OK, f_close(&fp));
	logger_exfat_filesystem_release(logger);

	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT,
		      dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL));

	/* Create upgrade file with larger version number */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app/2_3_1.bin", disk);
	zassert_equal(FR_OK, f_open(&fp, disk_path, FA_CREATE_NEW | FA_WRITE));
	sys_rand_get(input_buffer, 1024);
	zassert_equal(FR_OK, f_write(&fp, input_buffer, 1024, &written));
	zassert_equal(FR_OK, f_close(&fp));
	logger_exfat_filesystem_release(logger);

	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(2, upgrade_version.major);
	zassert_equal(3, upgrade_version.minor);
	zassert_equal(1, upgrade_version.revision);
	zassert_equal(0,
		      dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL));
	flash_area_validate(output_partition, input_buffer, 1024);

	/* Multiple larger version numbers */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app/5_1_0.bin", disk);
	zassert_equal(FR_OK, f_open(&fp, disk_path, FA_CREATE_NEW | FA_WRITE));
	sys_rand_get(input_buffer, 1569);
	zassert_equal(FR_OK, f_write(&fp, input_buffer, 1569, &written));
	zassert_equal(FR_OK, f_close(&fp));
	logger_exfat_filesystem_release(logger);

	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(5, upgrade_version.major);
	zassert_equal(1, upgrade_version.minor);
	zassert_equal(0, upgrade_version.revision);
	zassert_equal(0, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
						    progress_cb_validate));
	flash_area_validate(output_partition, input_buffer, 1569);
}

static bool test_data_init(const void *global_state)
{
	disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	return true;
}

static void partition_wipe(void *fixture)
{
	disk_access_erase(DISK_NAME, 0, sector_count);
}

ZTEST_SUITE(dfu_exfat, test_data_init, NULL, partition_wipe, NULL, NULL);
