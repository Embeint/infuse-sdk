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
#include <zephyr/sys/byteorder.h>

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

static void erase_progress_cb_validate(uint32_t erased, uint32_t total)
{
	static bool called;

	/* Only called once */
	zassert_false(called);
	zassert_equal(4096, erased);
	zassert_equal(4096, total);
	called = true;
}

static void write_progress_cb_validate(uint32_t written, uint32_t total)
{
	static size_t tracked_written;

	tracked_written = MIN(tracked_written + 512, total);
	zassert_equal(tracked_written, written);
	zassert_equal(1569, total);
}

int logger_exfat_init(const struct device *dev);

/* Write the first `len` bytes of input_buffer to a file */
static void image_write(const struct device *logger, const char *name, size_t len)
{
	char disk_path[64];
	const char *disk;
	UINT written;
	FIL fp;

	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app/%s", disk, name);
	zassert_equal(FR_OK, f_open(&fp, disk_path, FA_CREATE_ALWAYS | FA_WRITE));
	zassert_equal(FR_OK, f_write(&fp, input_buffer, len, &written));
	zassert_equal(len, written);
	zassert_equal(FR_OK, f_close(&fp));
	logger_exfat_filesystem_release(logger);
}

/* Create a file containing a MCUboot image header followed by random data */
static void image_create(const struct device *logger, const char *name, uint8_t major,
			 uint8_t minor, uint16_t revision, uint32_t build_num, size_t len)
{
	zassert_true(len >= 32 && len <= sizeof(input_buffer));
	sys_rand_get(input_buffer, len);
	sys_put_le32(0x96f3b83d, input_buffer);
	sys_put_le16(32, input_buffer + 8);
	sys_put_le32(len - 32, input_buffer + 12);
	input_buffer[20] = major;
	input_buffer[21] = minor;
	sys_put_le16(revision, input_buffer + 22);
	sys_put_le32(build_num, input_buffer + 24);
	image_write(logger, name, len);
}

static void upgrade_expect(const struct device *logger, uint8_t major, uint8_t minor,
			   uint16_t revision)
{
	struct infuse_version upgrade_version;

	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(major, upgrade_version.major);
	zassert_equal(minor, upgrade_version.minor);
	zassert_equal(revision, upgrade_version.revision);
}

ZTEST(dfu_exfat, test_dfu_image_find)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	int output_partition = PARTITION_ID(slot1_partition);
	struct infuse_version upgrade_version = {0};
	static uint8_t expected[sizeof(input_buffer)];
	char disk_path[64];
	const char *disk;
	FILINFO info;

	/* Initialise filesystem */
	zassert_equal(0, logger_exfat_init(logger));

	/* Run function without any folders */
	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
							  NULL, NULL));

	/* Create app image folder */
	disk = logger_exfat_filesystem_claim(logger, NULL, NULL, K_NO_WAIT);
	snprintf(disk_path, sizeof(disk_path), "%s:dfu", disk);
	zassert_equal(FR_OK, f_mkdir(disk_path));
	snprintf(disk_path, sizeof(disk_path), "%s:dfu/app", disk);
	zassert_equal(FR_OK, f_mkdir(disk_path));
	zassert_equal(FR_OK, f_stat(disk_path, &info));
	logger_exfat_filesystem_release(logger);

	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-ENOENT, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
							  NULL, NULL));

	/* Create upgrade file with smaller version number */
	image_create(logger, "1_7_12.bin", 1, 7, 12, 0, sizeof(input_buffer));
	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));

	/* Create upgrade file with same version number */
	image_create(logger, "2_1_4.bin", 2, 1, 4, 0, sizeof(input_buffer));
	zassert_equal(0, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));

	/* Create upgrade file with larger version number */
	image_create(logger, "2_3_1.bin", 2, 3, 1, 0, 1024);
	upgrade_expect(logger, 2, 3, 1);
	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(0, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL,
						    NULL));
	flash_area_validate(output_partition, input_buffer, 1024);

	/* Copying the same image again is rejected */
	zassert_equal(-EALREADY, dfu_exfat_app_upgrade_copy(logger, upgrade_version,
							    output_partition, NULL, NULL));

	/* Multiple larger version numbers */
	image_create(logger, "5_1_0.bin", 5, 1, 0, 0, 1569);
	memcpy(expected, input_buffer, 1569);
	upgrade_expect(logger, 5, 1, 0);
	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(0, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
						    erase_progress_cb_validate,
						    write_progress_cb_validate));
	flash_area_validate(output_partition, expected, 1569);
	zassert_equal(-EALREADY, dfu_exfat_app_upgrade_copy(logger, upgrade_version,
							    output_partition, NULL, NULL));

	/* Image version must match the file name */
	image_create(logger, "7_0_0.bin", 7, 0, 1, 0, 64);
	upgrade_expect(logger, 7, 0, 0);
	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(-EINVAL, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
							  NULL, NULL));
	flash_area_validate(output_partition, expected, 1569);

	/* File must contain a MCUboot image */
	image_create(logger, "7_0_0.bin", 7, 0, 0, 0, 64);
	input_buffer[0] ^= 0xFF;
	image_write(logger, "7_0_0.bin", 64);
	zassert_equal(-EINVAL, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
							  NULL, NULL));

	/* Image claiming to be larger than the file */
	image_create(logger, "7_0_0.bin", 7, 0, 0, 0, 64);
	sys_put_le32(64, input_buffer + 12);
	image_write(logger, "7_0_0.bin", 64);
	zassert_equal(-EINVAL, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition,
							  NULL, NULL));
	flash_area_validate(output_partition, expected, 1569);

	/* Valid image, upper case extension */
	image_create(logger, "8_0_0.BIN", 8, 0, 0, 0, 1023);
	upgrade_expect(logger, 8, 0, 0);
	zassert_equal(1, dfu_exfat_app_upgrade_exists(logger, &upgrade_version));
	zassert_equal(0, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL,
						    NULL));
	flash_area_validate(output_partition, input_buffer, 1023);

	/* Same version with a different build number can be retried */
	image_create(logger, "8_0_0.BIN", 8, 0, 0, 1, 1023);
	zassert_equal(0, dfu_exfat_app_upgrade_copy(logger, upgrade_version, output_partition, NULL,
						    NULL));
	flash_area_validate(output_partition, input_buffer, 1023);
}

static bool test_data_init(const void *global_state)
{
	disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	return true;
}

static void partition_wipe(void *fixture)
{
	disk_access_erase(DISK_NAME, 0, sector_count, DISK_ACCESS_ERASE_PHYSICAL);
}

ZTEST_SUITE(dfu_exfat, test_data_init, NULL, partition_wipe, NULL, NULL);
