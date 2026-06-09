/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/shim.h>

static uint8_t input_buffer[512 + 1] = {0};
static uint8_t output_buffer[3 * 512];

int logger_shim_init(const struct device *dev);

ZTEST(data_logger_api_4gb, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(10000000, state.physical_blocks);
	zassert_equal(512, state.block_size);
}

static int write_success_count;
static int write_success_data_type;
static int write_fail_count;
static int write_fail_data_type;
static int write_fail_reason;
static K_SEM_DEFINE(write_success, 0, 1);
static K_SEM_DEFINE(write_fail, 0, 1);

static void write_success_cb(const struct device *dev, enum infuse_type data_type, void *user_data)
{
	write_success_count += 1;
	write_success_data_type = data_type;
	zassert_equal(user_data, (void *)logger_shim_init);
	k_sem_give(&write_success);
}

static void write_failure_cb(const struct device *dev, enum infuse_type data_type, const void *mem,
			     uint16_t mem_len, int reason, void *user_data)
{
	write_fail_count += 1;
	write_fail_data_type = data_type;
	write_fail_reason = reason;
	zassert_equal(user_data, (void *)logger_shim_init);
	zassert_not_null(mem);
	zassert_not_equal(0, mem_len);
	k_sem_give(&write_fail);
}

struct data_logger_cb callbacks = {
	.write_success = write_success_cb,
	.write_failure = write_failure_cb,
};

static uint32_t first_read_block;
static uint16_t first_read_block_offset;
static uint16_t first_read_data_len;

static void first_read_cb(uint32_t block, uint16_t block_offset, uint16_t data_len)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);

	zassert_equal(first_read_block, block);
	zassert_equal(first_read_block_offset, block_offset);
	zassert_equal(first_read_data_len, data_len);

	/* Reset callback */
	data->read.read_cb = NULL;
}

ZTEST(data_logger_api_4gb, test_4gb_overflow)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	uint32_t phy_block_before_4gb;
	uint32_t block_before_4gb;
	uint64_t bytes_logged;
	int rc = 0;

	data_logger_get_state(logger, &state);
	zassert_equal(state.erase_unit, 2 * state.block_size);

	/* Manually set current block to just before rollover */
	block_before_4gb = UINT32_MAX / state.block_size;
	phy_block_before_4gb = block_before_4gb % state.physical_blocks;
	logger_shim_set_current_block(logger, block_before_4gb);

	/* Write a handful of blocks */
	for (int i = 0; i < 5; i++) {
		rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		data_logger_get_state(logger, &state);
		zassert_equal(block_before_4gb + i + 1, state.current_block);
		zassert_equal(i + 1, data->write.num_calls);
		zassert_equal(0x10, data->write.data_type);
		zassert_equal((phy_block_before_4gb + i) % state.physical_blocks,
			      data->write.block);
	}

	data_logger_get_state(logger, &state);
	bytes_logged = (uint64_t)state.current_block * state.block_size;
	zassert_true(bytes_logged > UINT32_MAX);

	/* Read back the blocks */
	for (int i = 0; i < 5; i++) {
		rc = data_logger_block_read(logger, block_before_4gb + i, 0, output_buffer,
					    state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		zassert_equal(i + 1, data->read.num_calls);
		zassert_equal((phy_block_before_4gb + i) % state.physical_blocks, data->read.block);
		zassert_equal(0, data->read.block_offset);
	}

	data->read.num_calls = 0;

	/* Read across the 4gb boundary */
	rc = data_logger_block_read(logger, block_before_4gb, 250, output_buffer, state.block_size);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(10));

	/* Validate second chunk of read */
	zassert_equal(1, data->read.num_calls);
	zassert_equal(phy_block_before_4gb, data->read.block);
	zassert_equal(250, data->read.block_offset);
	zassert_equal(state.block_size, data->read.data_len);
}

ZTEST(data_logger_api_4gb, test_wrap)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	uint32_t phy_block_before_wrap;
	uint32_t block_before_wrap;
	uint64_t bytes_logged;
	int rc = 0;

	data_logger_get_state(logger, &state);
	zassert_equal(state.erase_unit, 2 * state.block_size);

	/* Manually set current block to just before rollover */
	block_before_wrap = state.physical_blocks - 1;
	phy_block_before_wrap = block_before_wrap % state.physical_blocks;
	logger_shim_set_current_block(logger, block_before_wrap);

	data_logger_get_state(logger, &state);
	zassert_equal(block_before_wrap, state.current_block);

	/* Write a handful of blocks */
	for (int i = 0; i < 5; i++) {
		rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		data_logger_get_state(logger, &state);
		zassert_equal(block_before_wrap + i + 1, state.current_block);
		zassert_equal(i + 1, data->write.num_calls);
		zassert_equal(0x10, data->write.data_type);
		zassert_equal((phy_block_before_wrap + i) % state.physical_blocks,
			      data->write.block);
	}
	zassert_true(data->erase.num_calls > 0);

	data_logger_get_state(logger, &state);
	bytes_logged = (uint64_t)state.current_block * state.block_size;
	zassert_true(bytes_logged > UINT32_MAX);

	/* Read back the blocks */
	for (int i = 0; i < 5; i++) {
		rc = data_logger_block_read(logger, block_before_wrap + i, 0, output_buffer,
					    state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		zassert_equal(i + 1, data->read.num_calls);
		zassert_equal((phy_block_before_wrap + i) % state.physical_blocks,
			      data->read.block);
		zassert_equal(0, data->read.block_offset);
	}

	data->read.num_calls = 0;
	data->read.read_cb = first_read_cb;
	first_read_block = phy_block_before_wrap;
	first_read_block_offset = 250;
	first_read_data_len = state.block_size - 250;

	/* Read across the wrap boundary */
	rc = data_logger_block_read(logger, block_before_wrap, 250, output_buffer,
				    state.block_size);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	/* Validate first chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_is_null(data->read.read_cb);
	/* Validate second chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_equal(0, data->read.block);
	zassert_equal(0, data->read.block_offset);
	zassert_equal(250, data->read.data_len);

	data->read.num_calls = 0;
	data->read.read_cb = first_read_cb;
	first_read_block = phy_block_before_wrap;
	first_read_block_offset = 0;
	first_read_data_len = state.block_size;

	/* Only just read across the wrap boundary */
	rc = data_logger_block_read(logger, block_before_wrap, 0, output_buffer,
				    state.block_size + 1);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	/* Validate first chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_is_null(data->read.read_cb);
	/* Validate second chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_equal(0, data->read.block);
	zassert_equal(0, data->read.block_offset);
	zassert_equal(1, data->read.data_len);

	data->read.num_calls = 0;
	data->read.read_cb = first_read_cb;
	first_read_block = phy_block_before_wrap;
	first_read_block_offset = 100;
	first_read_data_len = state.block_size - 100;

	/* Only just read across the wrap boundary with offset */
	rc = data_logger_block_read(logger, block_before_wrap, 100, output_buffer,
				    state.block_size - 100 + 1);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	/* Validate first chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_is_null(data->read.read_cb);
	/* Validate second chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_equal(0, data->read.block);
	zassert_equal(0, data->read.block_offset);
	zassert_equal(1, data->read.data_len);

	data->read.num_calls = 0;
	data->read.read_cb = first_read_cb;
	first_read_block = phy_block_before_wrap;
	first_read_block_offset = 160;
	first_read_data_len = state.block_size - 160;

	/* Longer read across the wrap boundary */
	rc = data_logger_block_read(logger, block_before_wrap, 160, output_buffer,
				    sizeof(output_buffer));
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	/* Validate first chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_is_null(data->read.read_cb);
	/* Validate second chunk of read */
	zassert_equal(2, data->read.num_calls);
	zassert_equal(0, data->read.block);
	zassert_equal(0, data->read.block_offset);
	zassert_equal(sizeof(output_buffer) - (state.block_size - 160), data->read.data_len);
}

static void test_before(void *ignored)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);

	k_sleep(K_TICKS(1));
	logger_shim_init(logger);
	logger_shim_change_size(logger, 512);
	(void)k_sem_take(&write_success, K_NO_WAIT);
	(void)k_sem_take(&write_fail, K_NO_WAIT);
	write_success_count = 0;
	write_fail_count = 0;
	data->erase.block_until = NULL;
	data->reset.block_until = NULL;
}

ZTEST_SUITE(data_logger_api_4gb, NULL, NULL, test_before, NULL, NULL);
