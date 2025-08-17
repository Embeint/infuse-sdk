/**
 * @file
 * @copyright 2024 Embeint Inc
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
#include <zephyr/pm/device.h>

#include <infuse/identifiers.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/exfat.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>

#include <ff.h>

#define DISK_NAME DT_PROP(DT_PROP(DT_NODELABEL(data_logger_exfat), disk), disk_name)

static uint8_t input_buffer[1024] = {0};
static uint8_t output_buffer[1024];
static uint32_t sector_count;
static uint32_t sector_size;
static uint64_t device_id = 0x0123456789ABCDEF;

uint64_t vendor_infuse_device_id(void)
{
	return device_id;
}

int logger_exfat_init(const struct device *dev);

ZTEST(data_logger_exfat, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(512, state.block_size);
	zassert_equal(512, state.erase_unit);
	zassert_equal(sizeof(struct data_logger_persistent_block_header), state.block_overhead);
	zassert_equal(state.physical_blocks, state.logical_blocks);
	zassert_equal(0, sector_size % state.erase_unit);
	zassert_equal(0, state.erase_unit % state.block_size);
	zassert_true(state.requires_full_block_write);
}

ZTEST(data_logger_exfat, test_init_state)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	const char *const readme = DISK_NAME ":README.txt";
	struct data_logger_state state;
	FIL fp;

	/* Init all 0x00 */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);

	/* README file should exist */
	zassert_equal(FR_OK, f_open(&fp, readme, FA_READ));
	zassert_equal(FR_OK, f_close(&fp));
}

ZTEST(data_logger_exfat, test_bad_label)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	const char *const bad_label = DISK_NAME ":BADLABEL";
	struct data_logger_state state;

	/* Init and write some data */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));

	/* Set a bad label on the filesystem */
	zassert_equal(FR_OK, f_setlabel(bad_label));
	/* Re-init the filesystem */
	zassert_equal(0, logger_exfat_init(logger));
	/* Should be in a clean state again */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
}

static void test_sequence(bool reinit)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_persistent_block_header *header = (void *)output_buffer;
	struct data_logger_state state;
	uint8_t type = 0;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);

#ifdef CONFIG_DISK_DRIVER_SDMMC
	uint32_t max_blocks = 50;
#else
	uint32_t max_blocks = state.physical_blocks;
#endif

	for (int i = 0; i < max_blocks; i++) {
		/* Predictable block data per page */
		type = (i % 10) + 1;
		memset(input_buffer, i, sizeof(input_buffer));
		/* Write block to logger */
		zassert_equal(
			0, data_logger_block_write(logger, type, input_buffer, state.block_size));
		data_logger_get_state(logger, &state);
		zassert_equal(i + 1, state.current_block);
		/* Read block back from logger and check against input */
		zassert_equal(
			0, data_logger_block_read(logger, i, 0, output_buffer, state.block_size));
		zassert_equal(type, header->block_type);
		zassert_equal((i / state.physical_blocks) + 1, header->block_wrap);
		zassert_mem_equal(input_buffer + sizeof(*header), output_buffer + sizeof(*header),
				  sizeof(*header));

		/* Next file has no effect on the single file backend */
		zassert_equal(0, logger_exfat_file_next(logger));

		/* Reinit logger and validate state not lost */
		if (reinit) {
			zassert_equal(0, logger_exfat_init(logger));
			data_logger_get_state(logger, &state);
			zassert_equal(i + 1, state.current_block);
		}
	}

#ifndef CONFIG_DISK_DRIVER_SDMMC
	zassert_equal(-ENOMEM,
		      data_logger_block_write(logger, type, input_buffer, state.block_size));
#endif
}

ZTEST(data_logger_exfat, test_standard_operation)
{
	/* Test without rebooting each write */
	test_sequence(false);
}

ZTEST(data_logger_exfat, test_standard_operation_reinit)
{
	/* Test with rebooting each write */
	test_sequence(true);
}

ZTEST(data_logger_exfat, test_pm_behaviour)
{
#ifdef CONFIG_PM_DEVICE_RUNTIME
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;
	enum pm_device_state pm_state;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);

	/* Suspended after init */
	zassert_equal(0, pm_device_state_get(logger, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);

	/* Write block */
	zassert_equal(0, data_logger_block_write(logger, 0x02, input_buffer, state.block_size));

	/* Device should still be active for a short time after access */
	zassert_equal(0, pm_device_state_get(logger, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDING, pm_state);

	/* Suspended after some delay */
	k_sleep(K_MSEC(200));
	zassert_equal(0, pm_device_state_get(logger, &pm_state));
	zassert_equal(PM_DEVICE_STATE_SUSPENDED, pm_state);
#endif /* CONFIG_PM_DEVICE_RUNTIME */
}

ZTEST(data_logger_exfat, test_device_move)
{
	/* Test filesystem being moved between devices */
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	uint64_t first_id = infuse_device_id();
	struct data_logger_state state;
	char filename[40];
	uint8_t type = 3;
	FILINFO fno;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);

	/* Write 5 blocks */
	for (int i = 0; i < 5; i++) {
		zassert_equal(
			0, data_logger_block_write(logger, type, input_buffer, state.block_size));
	}
	data_logger_get_state(logger, &state);
	zassert_equal(5, state.current_block);

	/* Change the device ID */
	device_id += 1;

	/* Re-initialise logger, should fail */
	zassert_equal(-EBADF, logger_exfat_init(logger));

	/* First file should exist on filesystem, not second */
	snprintf(filename, sizeof(filename), "%s:infuse_%016llx_%06d.bin", DISK_NAME, first_id, 0);
	zassert_equal(FR_OK, f_stat(filename, &fno));
	snprintf(filename, sizeof(filename), "%s:infuse_%016llx_%06d.bin", DISK_NAME, first_id + 1,
		 0);
	zassert_equal(FR_NO_FILE, f_stat(filename, &fno));
}

static int erase_progress_calls;

static void erase_progress(uint32_t blocks_erased)
{
	erase_progress_calls += 1;
}

static void test_erase_blocks(uint32_t logged_blocks)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;
	uint8_t type = 3;
	int rc;

	erase_progress_calls = 0;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);

	/* Write requested blocks */
	for (int i = 0; i < logged_blocks; i++) {
		zassert_equal(
			0, data_logger_block_write(logger, type, input_buffer, state.block_size));
	}

	/* Erase the logger */
	rc = data_logger_erase(logger, true, erase_progress);
	zassert_equal(0, rc);

	/* Expected number of callbacks */
	zassert_equal(0, erase_progress_calls);

	/* Blocks should be reset, not the bytes logged count */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(logged_blocks * 512, state.bytes_logged);

	/* Re-initialise the logger, no data should exist */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
}

ZTEST(data_logger_exfat, test_erase)
{
	/* Test erasing all data */
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	test_erase_blocks(5);
	test_erase_blocks(state.physical_blocks / 2);
}

ZTEST(data_logger_exfat, test_kv_disk_info)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct kv_exfat_disk_info disk_info;

	/* Init logger */
	zassert_equal(0, logger_exfat_init(logger));

	/* Value should exist */
	zassert_equal(sizeof(struct kv_exfat_disk_info),
		      KV_STORE_READ(KV_KEY_EXFAT_DISK_INFO, &disk_info));

	/* Values should match disk query */
	zassert_equal(sector_count, disk_info.block_count);
	zassert_equal(sector_size, disk_info.block_size);
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

ZTEST_SUITE(data_logger_exfat, test_data_init, NULL, partition_wipe, NULL, NULL);
