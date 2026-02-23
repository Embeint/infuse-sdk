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
#include <zephyr/storage/disk_access.h>

#include <infuse/data_logger/logger.h>

#define DISK_NAME DT_PROP(DT_PROP(DT_NODELABEL(data_logger_disk_access), disk), disk_name)

#define NODE DT_NODELABEL(data_logger_disk_access)

static uint8_t input_buffer[1024] = {0};
static uint8_t output_buffer[1024];
static uint32_t sector_count;
static uint32_t sector_size;

int logger_disk_access_init(const struct device *dev);

ZTEST(data_logger_disk_access, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	zassert_equal(DATA_LOGGER_MAX_SIZE(NODE), CONFIG_DATA_LOGGER_DISK_ACCESS_MAX_SECTOR_SIZE);

	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.block_size);
	zassert_not_equal(0, state.erase_unit);
	zassert_equal(sizeof(struct data_logger_persistent_block_header), state.block_overhead);
	zassert_equal(254 * state.physical_blocks, state.logical_blocks);
	zassert_equal(0, state.erase_unit % state.block_size);
	zassert_equal(true, state.requires_full_block_write);
}

ZTEST(data_logger_disk_access, test_init_erased)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	/* Init all 0x00 */
	zassert_equal(0, logger_disk_access_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);
}

static void test_sequence(bool reinit)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_disk_access));
	struct data_logger_persistent_block_header *header = (void *)output_buffer;
	struct data_logger_state state;
	uint8_t type = 0;

	/* Init to erase value */
	zassert_equal(0, logger_disk_access_init(logger));
	data_logger_get_state(logger, &state);

#ifdef CONFIG_DISK_DRIVER_SDMMC
	uint32_t max_blocks = 50;
#else
	uint32_t max_blocks = 2 * state.physical_blocks;
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
			zassert_equal(0, logger_disk_access_init(logger));
			data_logger_get_state(logger, &state);
			zassert_equal(i + 1, state.current_block);
		}
	}

	if (!reinit) {
		/* If we didn't re-init on every loop, do it once at the end */
		zassert_equal(0, logger_disk_access_init(logger));
		data_logger_get_state(logger, &state);
		zassert_equal(2 * state.physical_blocks, state.current_block);
	}
}

ZTEST(data_logger_disk_access, test_standard_operation)
{
	/* Test without rebooting each write */
	test_sequence(false);
}

ZTEST(data_logger_disk_access, test_standard_operation_reinit)
{
	/* Test with rebooting each write */
	test_sequence(true);
}

static int erase_progress_calls;

static void erase_progress(uint32_t blocks_erased)
{
	erase_progress_calls += 1;
}

static void test_erase_blocks(uint32_t logged_blocks)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_disk_access));
	struct data_logger_state state;
	uint8_t type = 3;
	int rc;

	erase_progress_calls = 0;

	/* Init to erase value */
	zassert_equal(0, logger_disk_access_init(logger));
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
	zassert_true(erase_progress_calls > 0);

	/* Blocks should be reset, not the bytes logged count */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(logged_blocks * 512, state.bytes_logged);

	/* Re-initialise the logger, no data should exist */
	zassert_equal(0, logger_disk_access_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
}

ZTEST(data_logger_disk_access, test_erase)
{
	/* Test erasing all data */
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_disk_access));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	test_erase_blocks(5);
	test_erase_blocks(state.physical_blocks / 2);
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

ZTEST_SUITE(data_logger_disk_access, test_data_init, NULL, partition_wipe, NULL, NULL);
