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

#include <infuse/data_logger/logger.h>

static uint8_t input_buffer[1024] = {0};
static uint8_t output_buffer[1024];

ZTEST(data_logger_ram_buffering, test_basic_buffer)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_persistent_block_header *hdr = (void *)output_buffer;
	struct data_logger_state state;
	int rc;

	data_logger_get_state(logger, &state);

	/* 4kB buffer, 512 byte blocks (minus alignment testing), expect 7 blocks pending before any
	 * flush
	 */
	for (int i = 0; i < 7; i++) {
		/* Write block to logger */
		rc = data_logger_block_write(logger, 0x75 + i, input_buffer, state.block_size - i);
		zassert_equal(0, rc);

		data_logger_get_state(logger, &state);
		zassert_equal(0, state.current_block);
	}
	/* 8th block should trigger the flush of the 7 pended plus this */
	zassert_equal(0, data_logger_block_write(logger, 0x75 + 7, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(8, state.current_block);

	/* Read data back */
	for (int i = 0; i < 8; i++) {
		rc = data_logger_block_read(logger, i, 0, output_buffer, state.block_size);
		zassert_equal(0, rc);
		zassert_equal(0x75 + i, hdr->block_type);
	}

	/* Write two blocks to logger */
	zassert_equal(0, data_logger_block_write(logger, 0x75, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 0x76, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(8, state.current_block);

	/* Run the flush command */
	zassert_equal(0, data_logger_flush(logger));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(10, state.current_block);

	/* Data should exist on the backend */
	zassert_equal(0, data_logger_block_read(logger, 8, 0, output_buffer, state.block_size));
	zassert_equal(1, hdr->block_wrap);
	zassert_equal(0x75, hdr->block_type);
	zassert_equal(0, data_logger_block_read(logger, 9, 0, output_buffer, state.block_size));
	zassert_equal(1, hdr->block_wrap);
	zassert_equal(0x76, hdr->block_type);

	/* Run the flush command again, nothing should happen */
	zassert_equal(0, data_logger_flush(logger));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(10, state.current_block);
}

ZTEST_SUITE(data_logger_ram_buffering, NULL, NULL, NULL, NULL, NULL);
