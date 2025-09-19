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

static int write_fail_count;
static int write_fail_data_type;
static int write_fail_reason;
static K_SEM_DEFINE(write_fail, 0, 1);

static void write_failure(const struct device *dev, enum infuse_type data_type, const void *mem,
			  uint16_t mem_len, int reason, void *user_data)
{
	write_fail_count += 1;
	write_fail_data_type = data_type;
	write_fail_reason = reason;
	zassert_equal(user_data, (void *)write_failure);
	zassert_not_null(mem);
	zassert_not_equal(0, mem_len);
	k_sem_give(&write_fail);
}

struct data_logger_cb callbacks = {
	.write_failure = write_failure,
};

ZTEST(data_logger_api, test_write)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	int rc = 0;

	callbacks.user_data = write_failure;
	data_logger_register_cb(logger, &callbacks);

	/* Write to block 0 */
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);
	zassert_equal(1, data->write.num_calls);
	zassert_equal(0x10, data->write.data_type);
	zassert_equal(0, data->write.block);
	zassert_equal(state.block_size, data->write.data_len);

	/* Write to block 1 */
	rc = data_logger_block_write(logger, 0x11, input_buffer, state.block_size);
	zassert_equal(0, rc);
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(2, data->write.num_calls);
	zassert_equal(0x11, data->write.data_type);
	zassert_equal(1, data->write.block);
	zassert_equal(state.block_size, data->write.data_len);

	/* Write error */
	data->write.rc = -EINVAL;
	rc = data_logger_block_write(logger, 0x08, input_buffer, state.block_size);
	k_sleep(K_TICKS(1));
#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES
	/* Error occurs in other thread, wait for the callback */
	zassert_equal(0, rc);
	zassert_equal(0, k_sem_take(&write_fail, K_MSEC(1000)));
	rc = write_fail_reason;
#endif
	zassert_equal(-EINVAL, rc);
	zassert_equal(1, write_fail_count);
	zassert_equal(0x08, write_fail_data_type);

	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(3, data->write.num_calls);

	/* Reset backend error */
	data->write.rc = 0;

	/* Write more data than can fit */
	rc = data_logger_block_write(logger, 0x09, input_buffer, state.block_size + 1);
	k_sleep(K_TICKS(1));
	zassert_equal(-EINVAL, rc);
	zassert_equal(2, write_fail_count);
	zassert_equal(0x09, write_fail_data_type);

	/* Write to disconnected backend */
	logger_shim_change_size(logger, 0);

	rc = data_logger_block_write(logger, 0x1C, input_buffer, 10);
	k_sleep(K_TICKS(1));
	zassert_equal(-ENOTCONN, rc);
	zassert_equal(3, write_fail_count);
	zassert_equal(0x1C, write_fail_data_type);

	/* No calls to the backend */
	data_logger_get_state(logger, &state);
	zassert_equal(2, state.current_block);
	zassert_equal(3, data->write.num_calls);
}

ZTEST(data_logger_api, test_wrap)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	int rc = 0;

	data_logger_get_state(logger, &state);
	zassert_equal(state.erase_unit, 2 * state.block_size);

	/* Write all blocks */
	for (int i = 0; i < state.physical_blocks; i++) {
		rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		data_logger_get_state(logger, &state);
		zassert_equal(i + 1, state.current_block);
		zassert_equal(i + 1, data->write.num_calls);
		zassert_equal(0, data->erase.num_calls);
		zassert_equal(0x10, data->write.data_type);
		zassert_equal(i, data->write.block);
	}

	/* Try a write with a failing erase */
	data->erase.rc = -EIO;
	rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
#ifdef CONFIG_DATA_LOGGER_OFFLOAD_WRITES
	/* Error occurs in other thread */
	zassert_equal(0, rc);
#else
	zassert_equal(-EIO, rc);
#endif
	k_sleep(K_TICKS(1));

	/* Nothing written due to erase failure (write isn't called at all) */
	data_logger_get_state(logger, &state);
	zassert_equal(1, data->erase.num_calls);
	zassert_equal(state.physical_blocks, state.current_block);
	zassert_equal(state.physical_blocks, data->write.num_calls);

	/* Reset erase call counter to simplify maths */
	data->erase.num_calls = 0;
	data->erase.rc = 0;

	/* Continue writing with erase */
	for (int i = 0; i < state.physical_blocks / 2; i++) {
		rc = data_logger_block_write(logger, 0x11, input_buffer, state.block_size);
		zassert_equal(0, rc);
		k_sleep(K_TICKS(1));
		data_logger_get_state(logger, &state);
		zassert_equal(state.physical_blocks + i + 1, state.current_block);
		zassert_equal(state.physical_blocks + i + 1, data->write.num_calls);
		/* Every second block should result in an erase */
		zassert_equal((i / 2) + 1, data->erase.num_calls);
		/* Earliest block keeps jumping up */
		zassert_equal(ROUND_UP(i + 1, 2), state.earliest_block);
	}
}

ZTEST(data_logger_api, test_read)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;

	data_logger_get_state(logger, &state);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));

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
	k_sleep(K_TICKS(1));

	/* Erase without "erase_all" */
	zassert_equal(0, data_logger_erase(logger, false, NULL));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(512, state.bytes_logged);
	zassert_equal(1, data->reset.num_calls);
	zassert_equal(1, data->reset.block_hint);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));

	/* Erase with "erase_all" */
	zassert_equal(0, data_logger_erase(logger, true, NULL));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(1024, state.bytes_logged);
	zassert_equal(2, data->reset.num_calls);
	zassert_equal(128, data->reset.block_hint);

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));

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

	/* Write a block */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));
	zassert_equal(1, data->write.num_calls);

	/* Submit erase work */
	data->reset.block_until = &erase_sem;
	k_work_submit(&erase_work);
	k_sleep(K_TICKS(1));

	/* Try to write while erasing, no error, no call */
	zassert_equal(0, data_logger_block_write(logger, 0x10, input_buffer, state.block_size));
	k_sleep(K_TICKS(1));
	data_logger_get_state(logger, &state);
	zassert_equal(1, state.current_block);
	zassert_equal(1, data->write.num_calls);

	/* Read block that exists, error, no call */
	zassert_equal(-EBUSY,
		      data_logger_block_read(logger, 0, 0, output_buffer, state.block_size));
	zassert_equal(0, data->read.num_calls);

	/* Unblock the erase worker */
	k_sem_give(&erase_sem);
	data->reset.block_until = NULL;
}

static void do_writes(struct k_work *work)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_state state;
	int rc = 0;

	data_logger_get_state(logger, &state);

	for (int i = 0; i < state.physical_blocks + 1; i++) {
		rc = data_logger_block_write(logger, 0x10, input_buffer, state.block_size);
		zassert_equal(0, rc);
	}
}

ZTEST(data_logger_api, test_while_prepare)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);
	struct data_logger_state state;
	struct k_work erase_work;
	struct k_sem erase_sem;

	k_sem_init(&erase_sem, 0, 1);
	k_work_init(&erase_work, do_writes);
	data_logger_get_state(logger, &state);

	/* Submit block write work */
	data->erase.block_until = &erase_sem;
	k_work_submit(&erase_work);
	k_sleep(K_TICKS(100));

	/* Writing should currently be blocked in the erase step */
	data_logger_get_state(logger, &state);
	zassert_equal(state.physical_blocks, state.current_block);
	/* Earliest block should no longer be available, since we are actively erasing it */
	zassert_not_equal(0, state.earliest_block);

	/* Unblock the erase worker */
	k_sem_give(&erase_sem);
}

ZTEST(data_logger_api, test_flush)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);

	data->read.num_calls = 0;
	data->write.num_calls = 0;
	data->erase.num_calls = 0;

	/* Has no effect on loggers without an attached RAM buffer */
	zassert_equal(0, data_logger_flush(logger));

	/* No functions called */
	zassert_equal(0, data->read.num_calls);
	zassert_equal(0, data->write.num_calls);
	zassert_equal(0, data->erase.num_calls);
}

static void test_before(void *ignored)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_shim));
	struct data_logger_shim_function_data *data = data_logger_backend_shim_data_pointer(logger);

	k_sleep(K_TICKS(1));
	logger_shim_init(logger);
	logger_shim_change_size(logger, 512);
	(void)k_sem_take(&write_fail, K_NO_WAIT);
	write_fail_count = 0;
	data->erase.block_until = NULL;
	data->reset.block_until = NULL;
}

ZTEST_SUITE(data_logger_api, NULL, NULL, test_before, NULL, NULL);
