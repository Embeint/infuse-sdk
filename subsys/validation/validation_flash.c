/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <infuse/validation/core.h>
#include <infuse/validation/flash.h>

#define TEST "FLASH"

static uint8_t write_buffer[256];
static uint8_t read_buffer[256];

static int write_read_erase_page(const struct device *dev, size_t page, size_t page_size)
{
	size_t page_offset = sys_rand32_get() % (page_size - sizeof(write_buffer));
	off_t erase_offset = page * page_size;
	off_t write_offset = erase_offset + page_offset;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "Testing address %d.%d (0x%08lX)", page, page_offset,
			       write_offset);

	/* Fill buffer with random bytes */
	sys_rand_get(write_buffer, sizeof(write_buffer));

	/* Write the buffer */
	rc = flash_write(dev, write_offset, write_buffer, sizeof(write_buffer));
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "flash_write (%d)", rc);
		return rc;
	}

	/* Read it back */
	rc = flash_read(dev, write_offset, read_buffer, sizeof(read_buffer));
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "flash_read (%d)", rc);
		return rc;
	}

	/* Erase the page */
	rc = flash_erase(dev, erase_offset, page_size);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "flash_erase (%d)", rc);
		return rc;
	}

	/* Validate written == read */
	if (memcmp(write_buffer, read_buffer, sizeof(write_buffer)) != 0) {
		VALIDATION_REPORT_ERROR(TEST, "Data read != data written");
		return -EINVAL;
	}

	VALIDATION_REPORT_INFO(TEST, "Write-Read-Erase test passed");
	return 0;
}

int infuse_validation_flash(const struct device *dev, uint8_t flags)
{
	struct flash_pages_info info;
	size_t page_count;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DEV=%s", dev->name);

	/* Check init succeeded */
	if (!device_is_ready(dev)) {
		VALIDATION_REPORT_ERROR(TEST, "Device not ready");
		rc = -ENODEV;
		goto test_end;
	}

	/* Power up device */
	rc = pm_device_runtime_get(dev);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_get (%d)", rc);
		goto test_end;
	}

	if (flash_get_page_info_by_idx(dev, 0, &info) < 0) {
		VALIDATION_REPORT_ERROR(TEST, "flash_get_page_info_by_idx (%d)", rc);
		goto test_end;
	}
	page_count = flash_get_page_count(dev);
	VALIDATION_REPORT_VALUE(TEST, "PAGE_SIZE", "%d", info.size);
	VALIDATION_REPORT_VALUE(TEST, "PAGE_CNT", "%d", page_count);

	if (flags & VALIDATION_FLASH_DRIVER) {
		/* Pick a random page in the back half of the flash */
		size_t page = (sys_rand32_get() % (page_count / 2)) + (page_count / 2);

		rc = write_read_erase_page(dev, page, info.size);
	}

	if ((rc == 0) && (flags & VALIDATION_FLASH_CHIP_ERASE)) {
		VALIDATION_REPORT_INFO(TEST, "Erasing entire chip");

		rc = flash_erase(dev, 0, info.size * page_count);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "Chip erase complete");
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Chip erase failed (%d)", rc);
		}
	}

	/* Power down device */
	if (pm_device_runtime_put(dev) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "pm_device_runtime_put");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DEV=%s", dev->name);
	}

	return rc;
}
