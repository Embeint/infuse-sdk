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
#include <zephyr/pm/device.h>

#include <infuse/identifiers.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/exfat.h>

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
	uint8_t type;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);

#ifdef CONFIG_DISK_DRIVER_SDMMC
	uint32_t max_blocks = 50;
#else
	/* We lose an unpredicable number of blocks to file allocation tables.
	 * Actual loss depends on the size of binary files vs partition size.
	 * Treat 90% storage as a pass (due to small block sizes in testing).
	 */
	uint32_t max_blocks = 90 * state.physical_blocks / 100;
	uint32_t overhead_blocks = 10 * state.physical_blocks / 100;
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

		/* Reinit logger and validate state not lost */
		if (reinit) {
			zassert_equal(0, logger_exfat_init(logger));
			data_logger_get_state(logger, &state);
			zassert_equal(i + 1, state.current_block);
		}
	}

#ifndef CONFIG_DISK_DRIVER_SDMMC
	uint32_t failing_block = 0;
	int rc = 0;

	/* Somewhere in here we should get a write error */
	for (int i = 0; i < overhead_blocks; i++) {
		rc = data_logger_block_write(logger, 5, input_buffer, state.block_size);
		data_logger_get_state(logger, &state);

		/* We expect an expand to fail (-ENOMEM) followed by failures to write (-ENOMEM) */
		if (rc == -ENOMEM) {
			failing_block = state.current_block;

			/* Out of space, expect sizes to be truncated */
			data_logger_get_state(logger, &state);
			zassert_equal(failing_block, state.physical_blocks);
			zassert_equal(failing_block, state.logical_blocks);

			/* Try to write extra blocks */
			for (int j = 0; j < 5; j++) {
				rc = data_logger_block_write(logger, 6, input_buffer,
							     state.block_size);
				data_logger_get_state(logger, &state);
				zassert_equal(rc, -ENOMEM);
				zassert_equal(failing_block, state.current_block);
			}
			break;
		}
	}
	zassert_equal(-ENOMEM, rc);

	/* Re-initialise a full disk (Doesn't know we're out of memory) */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.physical_blocks);
	zassert_not_equal(0, state.logical_blocks);
	zassert_equal(failing_block, state.current_block);
	zassert_equal(0, state.earliest_block);

	/* But trying to write again will update state again */
	rc = data_logger_block_write(logger, 7, input_buffer, state.block_size);
	data_logger_get_state(logger, &state);
	zassert_equal(rc, -ENOMEM);
	zassert_equal(failing_block, state.current_block);
	zassert_equal(failing_block, state.physical_blocks);
	zassert_equal(failing_block, state.logical_blocks);
#endif

	zassert_equal(-ENOMEM, logger_exfat_file_next(logger));
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

ZTEST(data_logger_exfat, test_file_next)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	const uint32_t file_blocks = CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE / 512;
	struct data_logger_state state;

	/* Init to erase value */
	zassert_equal(0, logger_exfat_init(logger));

	/* Next file with no data logged, no change */
	zassert_equal(0, logger_exfat_file_next(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* With one block logger, should align to next file */
	zassert_equal(0, data_logger_block_write(logger, 5, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);

	zassert_equal(0, logger_exfat_file_next(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(file_blocks, state.current_block);

	/* Another align does nothing */
	zassert_equal(0, logger_exfat_file_next(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(file_blocks, state.current_block);

	/* Initialise recognises the file end */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(file_blocks, state.current_block);
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

	/* Re-initialise logger */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* Write 5 blocks */
	for (int i = 0; i < 5; i++) {
		zassert_equal(
			0, data_logger_block_write(logger, type, input_buffer, state.block_size));
	}
	data_logger_get_state(logger, &state);
	zassert_equal(5, state.current_block);

	/* Both files should exist on filesystem */
	snprintf(filename, sizeof(filename), "%s:infuse_%016llx_%06d.bin", DISK_NAME, first_id, 0);
	zassert_equal(FR_OK, f_stat(filename, &fno));
	snprintf(filename, sizeof(filename), "%s:infuse_%016llx_%06d.bin", DISK_NAME, first_id + 1,
		 0);
	zassert_equal(FR_OK, f_stat(filename, &fno));
}

static int erase_progress_calls;

static void erase_progress(uint32_t blocks_erased)
{
	erase_progress_calls += 1;
}

static void test_erase_blocks(uint32_t logged_blocks)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	uint32_t expected_files;
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
	expected_files = ROUND_UP(logged_blocks * 512, CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE) /
			 CONFIG_DATA_LOGGER_EXFAT_FILE_SIZE;

	/* Erase the logger */
	rc = data_logger_erase(logger, true, erase_progress);
	zassert_equal(0, rc);

	/* Expected number of callbacks */
	zassert_equal(expected_files, erase_progress_calls);

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

ZTEST(data_logger_exfat, test_reset)
{
	/* Test deleting "DELETE_TO_RESET.txt" */
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;
	char filename[40];
	uint8_t type = 3;

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

	/* Delete the sentinal file */
	snprintf(filename, sizeof(filename), "%s:DELETE_TO_RESET.txt", DISK_NAME);
	zassert_equal(FR_OK, f_unlink(filename));

	/* Re-initialise logger, contents should be erased */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
}

ZTEST(data_logger_exfat, test_filesystem_claim)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	uint8_t *block_buffer;
	size_t block_size;

	zassert_not_null(logger_exfat_filesystem_claim(logger, NULL, NULL, K_FOREVER));
	zassert_is_null(logger_exfat_filesystem_claim(logger, NULL, NULL, K_SECONDS(1)));
	logger_exfat_filesystem_release(logger);

	zassert_not_null(
		logger_exfat_filesystem_claim(logger, &block_buffer, &block_size, K_FOREVER));
	zassert_not_null(block_buffer);
	zassert_equal(512, block_size);
	logger_exfat_filesystem_release(logger);
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
