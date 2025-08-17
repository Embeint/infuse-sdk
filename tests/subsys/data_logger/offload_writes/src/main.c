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

#include <infuse/data_logger/logger.h>

#include <infuse/epacket/interface/epacket_dummy.h>

static uint8_t input_buffer[1024] = {0};
static uint8_t output_buffer[1024];

ZTEST(data_logger_write_offload, test_non_queued_default)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* Queue a block, we should return before it is actually written */
	zassert_equal(0, data_logger_block_write(logger, 0x23, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(-ENOENT,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));

	/* Wait for write to complete */
	k_sleep(K_MSEC(10));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);

	/* Validate block was eventually written */
	struct data_logger_persistent_block_header *hdr = (void *)output_buffer;

	zassert_equal(0, data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(0x23, hdr->block_type);

	/* Write a bunch of blocks to ensure queuing mechanism works properly and blocks not lost */
	for (int i = 0; i < 15; i++) {
		zassert_equal(0, data_logger_block_write(logger, 0x24 + i, input_buffer,
							 state.block_size));
	}
	/* Wait for writes to complete */
	k_sleep(K_MSEC(10));
	data_logger_get_state(logger, &state);
	zassert_equal(16, state.current_block);

	for (int i = 0; i < 15; i++) {
		zassert_equal(0, data_logger_block_read(logger, 1 + i, 0, output_buffer,
							state.block_size));
		zassert_equal(0x24 + i, hdr->block_type);
	}
}

ZTEST(data_logger_write_offload, test_queued_default)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_epacket));
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct data_logger_state state;
	struct net_buf *sent;

	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);

	/* Because backend already does queuing, block should be registered immediately */
	zassert_equal(0, data_logger_block_write(logger, 0x23, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);

	/* Validate packet was sent */
	sent = k_fifo_get(sent_queue, K_MSEC(1));
	zassert_not_null(sent);
	net_buf_unref(sent);
}

ZTEST_SUITE(data_logger_write_offload, NULL, NULL, NULL, NULL, NULL);
