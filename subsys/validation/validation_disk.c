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
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/crc.h>

#include <zephyr/device.h>
#include <infuse/validation/core.h>
#include <infuse/validation/disk.h>

#define TEST "DISK"

static uint8_t throughput_buffer[CONFIG_INFUSE_VALIDATION_DISK_THROUGHPUT_BUFFER_SIZE] __aligned(4);
static uint8_t write_buffer[512] __aligned(4);
static uint8_t read_buffer[512] __aligned(4);

static int write_read_erase_sector(const char *disk, uint32_t sector, uint32_t sector_size)
{
	int rc;

	if (sector_size > sizeof(write_buffer)) {
		VALIDATION_REPORT_INFO(TEST, "Sector too large (%d > %d)", sector_size,
				       sizeof(write_buffer));
		return -ENOMEM;
	}

	VALIDATION_REPORT_INFO(TEST, "Testing sector %d", sector);

	/* Fill buffer with random bytes */
	sys_rand_get(write_buffer, sizeof(write_buffer));

	/* Write the buffer */
	rc = disk_access_write(disk, write_buffer, sector, 1);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_write (%d)", rc);
		return rc;
	}

	/* Read it back */
	rc = disk_access_read(disk, read_buffer, sector, 1);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_read (%d)", rc);
		return rc;
	}

	/* Erase the page */
	rc = disk_access_erase(disk, sector, 1);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_erase (%d)", rc);
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

static int throughput_run(const char *disk, uint32_t sector_start, uint32_t sector_size)
{
	uint32_t num_sectors = sizeof(throughput_buffer) / sector_size;
	uint64_t buffer_offset = 0;
	k_ticks_t t_start, t_end;
	uint64_t throughput_bps;
	uint64_t duration_us;
	uint32_t data_crc;
	int rc;

	/* Fill buffer with random bytes */
	sys_rand_get(throughput_buffer, sizeof(throughput_buffer));
	data_crc = crc32_ieee(throughput_buffer, sizeof(throughput_buffer));

	/* Write a range of sectors one by one */
	VALIDATION_REPORT_INFO(TEST, "Write Throughput: %d sectors one by one", num_sectors);
	t_start = k_uptime_ticks();
	for (int i = 0; i < num_sectors; i++) {
		rc = disk_access_write(disk, throughput_buffer + buffer_offset, sector_start + i,
				       1);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "disk_access_read (%d)", rc);
			return rc;
		}
		buffer_offset += sector_size;
	}
	t_end = k_uptime_ticks();
	duration_us = k_ticks_to_us_near64(t_end - t_start);
	throughput_bps = (USEC_PER_SEC * buffer_offset) / duration_us;
	VALIDATION_REPORT_VALUE(TEST, "WTS_DURATION", "%lld us", duration_us);
	VALIDATION_REPORT_VALUE(TEST, "WTS_DATARATE", "%lld kB/s", throughput_bps / 1024);

	/* Write a range of sectors in a burst transaction */
	VALIDATION_REPORT_INFO(TEST, "Write Throughput: %d sectors burst", num_sectors);
	t_start = k_uptime_ticks();
	rc = disk_access_write(disk, throughput_buffer, sector_start + num_sectors, num_sectors);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_read (%d)", rc);
		return rc;
	}
	t_end = k_uptime_ticks();
	duration_us = k_ticks_to_us_near64(t_end - t_start);
	throughput_bps = (USEC_PER_SEC * (uint64_t)sector_size * num_sectors) / duration_us;
	VALIDATION_REPORT_VALUE(TEST, "WTM_DURATION", "%lld us", duration_us);
	VALIDATION_REPORT_VALUE(TEST, "WTM_DATARATE", "%lld kB/s", throughput_bps / 1024);

	/* Read a range of sectors one by one */
	VALIDATION_REPORT_INFO(TEST, "Read Throughput: %d sectors one by one", num_sectors);
	memset(throughput_buffer, 0x00, sizeof(throughput_buffer));
	buffer_offset = 0;
	t_start = k_uptime_ticks();
	for (int i = 0; i < num_sectors; i++) {
		rc = disk_access_read(disk, throughput_buffer + buffer_offset, sector_start + i, 1);
		if (rc < 0) {
			VALIDATION_REPORT_ERROR(TEST, "disk_access_read (%d)", rc);
			return rc;
		}
		buffer_offset += sector_size;
	}
	t_end = k_uptime_ticks();
	duration_us = k_ticks_to_us_near64(t_end - t_start);
	throughput_bps = (USEC_PER_SEC * buffer_offset) / duration_us;
	VALIDATION_REPORT_VALUE(TEST, "RTS_DURATION", "%lld us", duration_us);
	VALIDATION_REPORT_VALUE(TEST, "RTS_DATARATE", "%lld kB/s", throughput_bps / 1024);
	if (crc32_ieee(throughput_buffer, sizeof(throughput_buffer)) != data_crc) {
		VALIDATION_REPORT_ERROR(TEST, "Single block read throughput data corruption");
		return -EIO;
	}

	/* Read a range of sectors in a burst transaction */
	VALIDATION_REPORT_INFO(TEST, "Read Throughput: %d sectors burst", num_sectors);
	memset(throughput_buffer, 0x00, sizeof(throughput_buffer));
	t_start = k_uptime_ticks();
	rc = disk_access_read(disk, throughput_buffer, sector_start + num_sectors, num_sectors);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_read (%d)", rc);
		return rc;
	}
	t_end = k_uptime_ticks();
	duration_us = k_ticks_to_us_near64(t_end - t_start);
	throughput_bps = (USEC_PER_SEC * (uint64_t)sector_size * num_sectors) / duration_us;
	VALIDATION_REPORT_VALUE(TEST, "RTM_DURATION", "%lld us", duration_us);
	VALIDATION_REPORT_VALUE(TEST, "RTM_DATARATE", "%lld kB/s", throughput_bps / 1024);
	if (crc32_ieee(throughput_buffer, sizeof(throughput_buffer)) != data_crc) {
		VALIDATION_REPORT_ERROR(TEST, "Burst block read throughput data corruption");
		return -EIO;
	}

	/* Cleanup the disk sectors we used */
	rc = disk_access_erase(disk, sector_start, 2 * num_sectors);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "disk_access_erase (%d)", rc);
		return rc;
	}
	return 0;
}

int infuse_validation_disk(const char *disk, uint8_t flags)
{
	uint32_t sector_count, sector_size;
	int rc;

	VALIDATION_REPORT_INFO(TEST, "DISK=%s", disk);

	rc = disk_access_ioctl(disk, DISK_IOCTL_CTRL_INIT, NULL);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to init (%d)", rc);
		goto test_end;
	}

	rc = disk_access_ioctl(disk, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to query sector count (%d)", rc);
		goto test_end;
	}
	rc = disk_access_ioctl(disk, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (rc < 0) {
		VALIDATION_REPORT_ERROR(TEST, "Failed to query sector size (%d)", rc);
		goto test_end;
	}

	VALIDATION_REPORT_INFO(TEST, "Sector Count: %d", sector_count);
	VALIDATION_REPORT_INFO(TEST, " Sector Size: %d", sector_size);

	if (sector_count == 0 || sector_size == 0) {
		VALIDATION_REPORT_ERROR(TEST, "Invalid disk parameters");
		rc = -EINVAL;
		goto test_end;
	}

	if (flags & VALIDATION_DISK_DRIVER) {
		/* Pick a random sector in the back half of the disk */
		uint32_t sector = (sys_rand32_get() % (sector_count / 2)) + (sector_count / 2);

		rc = write_read_erase_sector(disk, sector, sector_size);
	}
	if ((rc == 0) && (flags & VALIDATION_DISK_THROUGHPUT)) {
		/* Pick a random sector in the back half of the disk */
		uint32_t sector = (sys_rand32_get() % (sector_count / 2)) + (sector_count / 2);

		/* Shift sector slightly forward so we can't run off the back */
		rc = throughput_run(disk, sector - 256, sector_size);
	}

	if ((rc == 0) && (flags & VALIDATION_DISK_ERASE)) {
		VALIDATION_REPORT_INFO(TEST, "Erasing entire disk");

		rc = disk_access_erase(disk, 0, sector_count);
		if (rc == 0) {
			VALIDATION_REPORT_INFO(TEST, "Disk erase complete");
		} else {
			VALIDATION_REPORT_ERROR(TEST, "Disk erase failed (%d)", rc);
		}
	}

	if (disk_access_ioctl(disk, DISK_IOCTL_CTRL_DEINIT, NULL) < 0) {
		if (rc == 0) {
			VALIDATION_REPORT_ERROR(TEST, "Failed to deinit");
			rc = -EIO;
		}
	}
test_end:
	if (rc == 0) {
		VALIDATION_REPORT_PASS(TEST, "DISK=%s", disk);
	}

	return rc;
}
