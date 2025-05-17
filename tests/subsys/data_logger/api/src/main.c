/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/backend/shim.h>

static uint8_t input_buffer[512] = {0};
static uint8_t output_buffer[512];

int logger_shim_init(const struct device *dev);

ZTEST(data_logger_api, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(128, state.physical_blocks);
	zassert_equal(512, state.block_size);
}

ZTEST(data_logger_api, test_write)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;

	/* Write to block 0 */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);
	zassert_equal(1, data->write.num_calls);
	zassert_equal(0x10, data->write.data_type);
	zassert_equal(0, data->write.block);
	zassert_equal(state.block_size, data->write.data_len);

	/* Write to block 1 */
	zassert_equal(0, data_logger_block_write(logger, 0x11, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(2, data->write.num_calls);
	zassert_equal(0x11, data->write.data_type);
	zassert_equal(1, data->write.block);
	zassert_equal(state.block_size, data->write.data_len);

	/* Write error */
	data->write.rc = -EINVAL;
	zassert_equal(-EINVAL,
		      data_logger_block_write(logger, 0x08, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(3, data->write.num_calls);
}

ZTEST(data_logger_api, test_read)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	data->read.num_calls = 0;

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));

	/* Read block that exists */
	zassert_equal(0, data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(1, data->read.num_calls);

	/* Read block that doesn't exists (call doesn't make it to backend) */
	zassert_equal(-ENOENT,
		      data_logger_block_read(logger, 1, 0, output_buffer, state.block_size));
	zassert_equal(1, data->read.num_calls);

	/* Read error */
	data->read.rc = -EINVAL;
	zassert_equal(-EINVAL,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(2, data->read.num_calls);
}

ZTEST(data_logger_api, test_erase)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));

	/* Erase without "erase_all" */
	zassert_equal(0, data_logger_erase(logger, false, NULL));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(512, state.bytes_logged);
	zassert_equal(1, data->reset.num_calls);
	zassert_equal(1, data->reset.block_hint);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));

	/* Erase with "erase_all" */
	zassert_equal(0, data_logger_erase(logger, true, NULL));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(1024, state.bytes_logged);
	zassert_equal(2, data->reset.num_calls);
	zassert_equal(128, data->reset.block_hint);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));

	/* Erase error */
	data->reset.rc = -EIO;
	zassert_equal(-EIO, data_logger_erase(logger, true, NULL));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);
	zassert_equal(1536, state.bytes_logged);
	zassert_equal(3, data->reset.num_calls);
}

static void do_erase(struct k_work *work)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));

	zassert_equal(0, data_logger_erase(logger, true, NULL));
}

ZTEST(data_logger_api, test_while_erase)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	struct k_work erase_work;
	struct k_sem erase_sem;

	k_sem_init(&erase_sem, 0, 1);
	k_work_init(&erase_work, do_erase);
	data_logger_get_state(logger, &state);
	data->read.num_calls = 0;

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	zassert_equal(1, data->write.num_calls);

	/* Submit erase work */
	data->reset.block_until = &erase_sem;
	k_work_submit(&erase_work);
	k_sleep(K_TICKS(1));

	/* Try to write while erasing, no error, no call */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);
	zassert_equal(1, data->write.num_calls);

	/* Read block that exists, error, no call */
	zassert_equal(-EBUSY,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(0, data->read.num_calls);

	/* Unblock the erase worker */
	k_sem_give(&erase_sem);
}

static void test_before(void *ignored)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));

	logger_shim_init(logger);
}

ZTEST_SUITE(data_logger_api, NULL, NULL, test_before, NULL, NULL);
