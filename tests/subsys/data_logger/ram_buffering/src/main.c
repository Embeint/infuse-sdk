/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
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
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	/* 4kB buffer, 512 byte blocks (minus alignment testing), expect 7 blocks pending before any
	 * flush
	 */
	for (int i = 0; i < 7; i++) {
		/* Write block to logger */
		zassert_equal(0, data_logger_block_write(logger, 0x75 + i, input_buffer,
							 state.block_size - i));

		data_logger_get_state(logger, &state);
		zassert_equal(0, state.current_block);
	}
	/* 8th block should trigger the flush of the 7 pended plus this */
	zassert_equal(0, data_logger_block_write(logger, 0x75 + 7, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(8, state.current_block);

	/* Read data back */
	for (int i = 0; i < 8; i++) {
		struct data_logger_persistent_block_header *hdr = (void *)output_buffer;

		zassert_equal(
			0, data_logger_block_read(logger, i, 0, output_buffer, state.block_size));
		zassert_equal(0x75 + i, hdr->block_type);
	}
}

ZTEST_SUITE(data_logger_ram_buffering, NULL, NULL, NULL, NULL, NULL);
