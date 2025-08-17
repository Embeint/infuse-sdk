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

#define DISK_NAME DT_PROP(DT_PROP(DT_NODELABEL(data_logger_exfat), disk), disk_name)

static uint8_t input_buffer[512] = {0};
static uint8_t output_buffer[512];
static uint32_t sector_count;
static uint32_t sector_size;
static uint64_t device_id = 0x0123456789ABCDEF;

uint64_t vendor_infuse_device_id(void)
{
	return device_id;
}

int logger_exfat_init(const struct device *dev);

static void log_one_ram_buffer(const struct device *logger, uint32_t base_block,
			       uint16_t block_size)
{
	struct data_logger_state state;

	for (int i = 0; i < 6; i++) {
		/* Write block to logger */
		zassert_equal(0,
			      data_logger_block_write(logger, 0x75 + i, input_buffer, block_size));
		k_sleep(K_TICKS(1));
		data_logger_get_state(logger, &state);
		zassert_equal(base_block, state.current_block);
	}
	/* 8th block should trigger the flush of the 7 pended plus this */
	zassert_equal(0, data_logger_block_write(logger, 0x75 + 6, input_buffer, block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(base_block + 7, state.current_block);

	/* Read data back */
	for (int i = 0; i < 7; i++) {
		struct data_logger_persistent_block_header *hdr = (void *)output_buffer;

		zassert_equal(0, data_logger_block_read(logger, base_block + i, 0, output_buffer,
							block_size));
		zassert_equal(1, hdr->block_wrap);
		zassert_equal(0x75 + i, hdr->block_type);
	}
}

ZTEST(data_logger_ram_buffering_disk, test_init_state)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;
	uint32_t failing_block = 0;
	int rc = 0, i;

	/* Init all 0x00 */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);

	/* Log a whole bunch of blocks, checking validity */
	for (i = 0; i < state.logical_blocks - 400; i += 7) {
		log_one_ram_buffer(logger, i, state.block_size);
	}

	/* Test re-initialising at many blocks written */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.physical_blocks);
	zassert_equal(0, state.earliest_block);
	zassert_equal(i, state.current_block);

	/* Push the remainder of the blocks, at some point this should fail */
	while (i++ < state.logical_blocks) {
		rc = data_logger_block_write(logger, 0x75, input_buffer, state.block_size);
		if (rc == -ENOMEM) {
			data_logger_get_state(logger, &state);
			failing_block = state.current_block;
			break;
		}
	}
#ifdef CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE
	zassert_equal(-ENOMEM, rc);
#else
	zassert_equal(0, rc);
#endif /* CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE */

	/* Re-initialise a full disk (Doesn't know we're out of memory) */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.physical_blocks);
	zassert_not_equal(0, state.logical_blocks);
	zassert_equal(0, state.earliest_block);

#ifdef CONFIG_DATA_LOGGER_EXFAT_SINGLE_FILE
	zassert_equal(state.physical_blocks, state.current_block);
#endif /* CONFIG_DATA_LOGGER_EXFAT_SINGLE_FILE */

#ifdef CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE
	/* Doesn't match exactly since the logger doesn't recognise
	 * partial RAM buffer successful writes.
	 */
	zassert_within(failing_block, state.current_block, 7);

	/* Because the multi file backend has looser knowledge of logger limits,
	 * it takes until we actually try to write to detect that the logger is full.
	 * Prime RAM buffer here so the next call will try to write.
	 */
	for (int i = 0; i < 6; i++) {
		rc = data_logger_block_write(logger, 7, input_buffer, state.block_size);
		zassert_equal(0, rc);
	}
#endif /* CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE */
	/* But trying to write again will update state again */
	rc = data_logger_block_write(logger, 7, input_buffer, state.block_size);
	zassert_equal(-ENOMEM, rc);
	data_logger_get_state(logger, &state);
	zassert_equal(state.physical_blocks, state.current_block);
	zassert_equal(state.physical_blocks, state.logical_blocks);
#ifdef CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE
	/* Doesn't match exactly since the logger doesn't recognise
	 * partial RAM buffer successful writes.
	 */
	zassert_within(state.physical_blocks, failing_block, 7);
#endif /* CONFIG_DATA_LOGGER_EXFAT_MULTI_FILE */
}

ZTEST(data_logger_ram_buffering_disk, test_flush)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_persistent_block_header *hdr = (void *)output_buffer;
	struct data_logger_state state;

	/* Init all 0x00 */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);

	/* Write two blocks to logger */
	zassert_equal(0, data_logger_block_write(logger, 0x75, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 0x76, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* Run the flush command */
	zassert_equal(0, data_logger_flush(logger));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);

	/* Data should exist on the backend */
	zassert_equal(0, data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(1, hdr->block_wrap);
	zassert_equal(0x75, hdr->block_type);
	zassert_equal(0, data_logger_block_read(logger, 1, 0, output_buffer, state.block_size));
	zassert_equal(1, hdr->block_wrap);
	zassert_equal(0x76, hdr->block_type);

	/* Run the flush command again, nothing should happen */
	zassert_equal(0, data_logger_flush(logger));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);

	/* Re-init should work */
	zassert_equal(0, logger_exfat_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);

	/* Write more blocks */
	log_one_ram_buffer(logger, 2, state.block_size);
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

ZTEST_SUITE(data_logger_ram_buffering_disk, test_data_init, NULL, partition_wipe, NULL, NULL);
