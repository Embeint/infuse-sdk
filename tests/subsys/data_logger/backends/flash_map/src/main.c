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

#include <infuse/data_logger/logger.h>

#define NODE DT_NODELABEL(data_logger_flash)

static uint8_t input_buffer[2 * DT_PROP(NODE, block_size)] = {0};
static uint8_t output_buffer[2 * DT_PROP(NODE, block_size)];
static uint8_t *flash_buffer;
static size_t flash_buffer_size;

int logger_flash_map_init(const struct device *dev);

ZTEST(data_logger_flash_map, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(DT_PROP(NODE, block_size), state.block_size);
	zassert_not_equal(0, state.erase_unit);
	zassert_equal(sizeof(struct data_logger_persistent_block_header), state.block_overhead);
	zassert_equal(flash_buffer_size / state.block_size, state.physical_blocks);
	zassert_equal(254 * state.physical_blocks, state.logical_blocks);
	zassert_equal(0, flash_buffer_size % state.erase_unit);
	zassert_equal(0, state.erase_unit % state.block_size);
	zassert_equal(DT_PROP(NODE, full_block_write), state.requires_full_block_write);
}

ZTEST(data_logger_flash_map, test_init_erased)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	/* Init all 0x00 */
	memset(flash_buffer, 0x00, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.bytes_logged);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);

	/* Init all 0xFF */
	memset(flash_buffer, 0xFF, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.bytes_logged);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);
}

ZTEST(data_logger_flash_map, test_init_erased_invalid_start)
{
	const struct device *logger = DEVICE_DT_GET(NODE);

	/* Init all 0x00, set the first byte to a valid wrap count */
	memset(flash_buffer, 0x00, flash_buffer_size);
	flash_buffer[0] = 0x04;
	zassert_equal(-EINVAL, logger_flash_map_init(logger));
}

ZTEST(data_logger_flash_map, test_init_part_written)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.block_size);

	memset(flash_buffer, 0x00, flash_buffer_size);

	for (int i = 1; i < flash_buffer_size / state.block_size; i++) {
		memset(flash_buffer, 0x01, i * state.block_size);
		zassert_equal(0, logger_flash_map_init(logger));
		data_logger_get_state(logger, &state);
		zassert_equal(0, state.bytes_logged);
		zassert_equal(i, state.boot_block);
		zassert_equal(i, state.current_block);
		zassert_equal(0, state.earliest_block);
	}
}

ZTEST(data_logger_flash_map, test_init_all_written)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	/* Init all 0x01 */
	memset(flash_buffer, 0x01, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0x01 * state.physical_blocks, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);

	/* Init all 0x20 */
	memset(flash_buffer, 0x20, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0x20 * state.physical_blocks, state.current_block);
	zassert_equal(0x1F * state.physical_blocks, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);
}

ZTEST(data_logger_flash_map, test_init_all_written_with_start_erase)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;
	uint16_t blocks_in_erase;

	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.block_size);
	blocks_in_erase = state.erase_unit / state.block_size;

	/* Init all 0x04, pre-erase next block */
	memset(flash_buffer, 0x04, flash_buffer_size);
	memset(flash_buffer, 0x00, state.erase_unit);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0x04 * state.physical_blocks, state.current_block);
	zassert_equal(0x03 * state.physical_blocks + blocks_in_erase, state.earliest_block);

	/* Start writing next blocks */
	for (int i = 1; i <= blocks_in_erase; i++) {
		memset(flash_buffer, 0x05, state.block_size * i);
		zassert_equal(0, logger_flash_map_init(logger));
		data_logger_get_state(logger, &state);
		zassert_equal(0x04 * state.physical_blocks + i, state.current_block);
		zassert_equal(0x03 * state.physical_blocks + blocks_in_erase, state.earliest_block);
	}
}

ZTEST(data_logger_flash_map, test_init_all_written_with_end_erase)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	uint8_t *flash_buffer_end = flash_buffer + flash_buffer_size;
	struct data_logger_state state;
	uint16_t blocks_in_erase;

	data_logger_get_state(logger, &state);
	zassert_not_equal(0, state.block_size);
	blocks_in_erase = state.erase_unit / state.block_size;

	/* Init all 0x04, pre-erase last block */
	memset(flash_buffer, 0x04, flash_buffer_size);
	memset(flash_buffer_end - state.erase_unit, 0x00, state.erase_unit);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0x04 * state.physical_blocks - blocks_in_erase, state.current_block);
	zassert_equal(0x03 * state.physical_blocks, state.earliest_block);

	/* Start writing next blocks */
	for (int i = 1; i <= blocks_in_erase; i++) {
		memset(flash_buffer_end - state.erase_unit, 0x04, state.block_size * i);
		zassert_equal(0, logger_flash_map_init(logger));
		data_logger_get_state(logger, &state);
		zassert_equal(0x04 * state.physical_blocks - blocks_in_erase + i,
			      state.current_block);
		zassert_equal(0x03 * state.physical_blocks, state.earliest_block);
	}
}

ZTEST(data_logger_flash_map, test_write_errors)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	/* Init full */
	memset(flash_buffer, 0xFE, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);

	zassert_equal(-EINVAL,
		      data_logger_block_write(logger, 0x10, input_buffer, state.block_size + 1));
	zassert_equal(-ENOMEM,
		      data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
}

ZTEST(data_logger_flash_map, test_read_errors)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	/* Init part full */
	memset(flash_buffer, 0x04, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);

	/* Start of logger */
	zassert_equal(-ENOENT,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	/* Just before start of data */
	zassert_equal(-ENOENT, data_logger_block_read(logger, 0x03 * state.physical_blocks - 1, 0,
						      output_buffer, state.block_size));
	/* Just after end of data */
	zassert_equal(-ENOENT, data_logger_block_read(logger, 0x04 * state.physical_blocks, 0,
						      output_buffer, state.block_size));
	/* Reading from valid data into invalid data */
	zassert_equal(-ENOENT, data_logger_block_read(logger, 0x05 * state.physical_blocks - 1, 0,
						      output_buffer, 2 * state.block_size));
	/* Invalid block offset */
	zassert_equal(-ENOENT,
		      data_logger_block_read(logger, 0x03 * state.physical_blocks, state.block_size,
					     output_buffer, state.block_size));
}

ZTEST(data_logger_flash_map, test_read_wrap)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;
	uint16_t block_size = DT_PROP(NODE, block_size);
	uint16_t block_short = block_size - 12;

	/* Init half 0x02, half 0x01 */
	memset(flash_buffer, 0x01, flash_buffer_size);
	memset(flash_buffer, 0x02, flash_buffer_size / 2);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(3 * state.physical_blocks / 2, state.current_block);
	zassert_equal(state.physical_blocks / 2, state.earliest_block);

	/* Read across block boundary */
	zassert_equal(0, data_logger_block_read(logger, state.physical_blocks, 200, output_buffer,
						state.block_size));
	memset(input_buffer, 0x02, state.block_size);
	zassert_mem_equal(input_buffer, output_buffer, state.block_size);

	/* Read across wrap around boundary */
	zassert_equal(0, data_logger_block_read(logger, state.physical_blocks - 1, 0, output_buffer,
						2 * state.block_size));
	memset(input_buffer, 0x01, state.block_size);
	memset(input_buffer + state.block_size, 0x02, state.block_size);
	zassert_mem_equal(input_buffer, output_buffer, 2 * state.block_size);

	/* Read across wrap around boundary with offset */
	zassert_equal(0, data_logger_block_read(logger, state.physical_blocks - 1, block_short,
						output_buffer, 20));
	memset(input_buffer, 0x01, state.block_size - block_short);
	memset(input_buffer + state.block_size - block_short, 0x02,
	       20 - (state.block_size - block_short));
	zassert_mem_equal(input_buffer, output_buffer, 20);
}

static void test_sequence(bool reinit)
{
	const struct flash_parameters *params =
		flash_get_parameters(DEVICE_DT_GET(DT_NODELABEL(sim_flash)));
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_persistent_block_header *header = (void *)output_buffer;
	struct data_logger_state state;
	uint8_t type;

	/* Init to erase value */
	memset(flash_buffer, params->erase_value, flash_buffer_size);
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);

	for (int i = 0; i < 254 * state.physical_blocks; i++) {
		/* Predicatable block data per page */
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
			zassert_equal(0, logger_flash_map_init(logger));
			data_logger_get_state(logger, &state);
			zassert_equal(i + 1, state.boot_block);
			zassert_equal(i + 1, state.current_block);
		}
	}
}

ZTEST(data_logger_flash_map, test_standard_operation)
{
	/* Test without rebooting each write */
	test_sequence(false);
	/* Test with rebooting each write */
	test_sequence(true);
}

static int erase_progress_calls;

static void erase_progress(uint32_t blocks_erased)
{
	erase_progress_calls += 1;
}

static void test_erase_blocks(uint32_t logged_blocks, bool erase_all)
{
	const struct device *logger = DEVICE_DT_GET(NODE);
	uint16_t block_size = DT_PROP(NODE, block_size);
	struct data_logger_state state;
	uint8_t type = 3;
	int rc;

	erase_progress_calls = 0;

	/* Init to erase value */
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);

	/* Write requested blocks */
	for (int i = 0; i < logged_blocks; i++) {
		zassert_equal(
			0, data_logger_block_write(logger, type, input_buffer, state.block_size));
	}

	/* Erase the logger */
	rc = data_logger_erase(logger, erase_all, erase_progress);
	zassert_equal(0, rc);

	/* Expected number of callbacks */
	zassert_true(erase_progress_calls > 0);

	/* Blocks should be reset, not the bytes logged count */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
	zassert_equal(logged_blocks * block_size, state.bytes_logged);

	/* Re-initialise the logger, no data should exist */
	zassert_equal(0, logger_flash_map_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.boot_block);
	zassert_equal(0, state.current_block);
}

ZTEST(data_logger_flash_map, test_erase)
{
	/* Test erasing all data */
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	/* Erasing entire flash space */
	test_erase_blocks(5, true);
	test_erase_blocks(state.physical_blocks / 2, true);
	test_erase_blocks(3 * state.physical_blocks / 2, true);

	/* Erasing only logged data */
	test_erase_blocks(5, false);
	test_erase_blocks(state.physical_blocks / 2, false);
	test_erase_blocks(3 * state.physical_blocks / 2, false);
}

ZTEST(data_logger_flash_map, test_erase_exclusion)
{
	/* Test read/write while erasing */
	const struct device *logger = DEVICE_DT_GET(NODE);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* Write some blocks to start */
	for (int i = 0; i < 3; i++) {
		zassert_equal(0,
			      data_logger_block_write(logger, 1, input_buffer, state.block_size));
	}
	/* Nothing actually written */
	data_logger_get_state(logger, &state);
	zassert_equal(3, state.current_block);

	data_logger_set_erase_state(logger, true);

	/* Try to write some blocks */
	zassert_equal(0, data_logger_block_write(logger, 1, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 1, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 1, input_buffer, state.block_size));

	/* Extra blocks not actually written */
	data_logger_get_state(logger, &state);
	zassert_equal(3, state.current_block);

	/* Reading returns errors */
	zassert_equal(-EBUSY,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(-EBUSY,
		      data_logger_block_read(logger, 1, 0, output_buffer, state.block_size));

	/* Clear the erasing state*/
	data_logger_set_erase_state(logger, false);

	/* Reading works again */
	zassert_equal(0, data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(0, data_logger_block_read(logger, 1, 0, output_buffer, state.block_size));

	/* So does writing (not actually reset because we simulated the flag) */
	zassert_equal(0, data_logger_block_write(logger, 1, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(4, state.current_block);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

ZTEST_SUITE(data_logger_flash_map, test_data_init, NULL, NULL, NULL, NULL);
