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
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>

#include <infuse/data_logger/logger.h>

#include <ff.h>

#define DISK_NAME DT_PROP(DT_PROP(DT_NODELABEL(data_logger_exfat), disk), disk_name)

static uint8_t input_buffer[1024] = {0};
static uint8_t output_buffer[1024];
static uint8_t *memory;
static size_t memory_size;

int data_logger_init(const struct device *dev);

ZTEST(data_logger_exfat, test_init_constants)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;

	data_logger_get_state(logger, &state);
	zassert_equal(512, state.block_size);
	zassert_equal(512, state.erase_unit);
	zassert_equal(sizeof(struct data_logger_persistent_block_header), state.block_overhead);
	zassert_equal(memory_size / state.block_size, state.physical_blocks);
	zassert_equal(state.physical_blocks, state.logical_blocks);
	zassert_equal(0, memory_size % state.erase_unit);
	zassert_equal(0, state.erase_unit % state.block_size);
}

ZTEST(data_logger_exfat, test_init_state)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	struct data_logger_state state;

	/* Init all 0x00 */
	zassert_equal(0, data_logger_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, state.current_block);
	zassert_equal(0, state.earliest_block);
	zassert_not_equal(0, state.physical_blocks);
}

ZTEST(data_logger_exfat, test_readme_exists)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	const char *readme = DISK_NAME ":README.txt";
	FIL fp;

	zassert_equal(0, data_logger_init(logger));

	/* File should exist */
	zassert_equal(FR_OK, f_open(&fp, readme, FA_READ));
	zassert_equal(FR_OK, f_close(&fp));
}

ZTEST(data_logger_exfat, test_bad_label)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
	const char *bad_label = DISK_NAME ":BADLABEL";
	struct data_logger_state state;

	/* Init and write some data */
	zassert_equal(0, data_logger_init(logger));
	data_logger_get_state(logger, &state);
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));
	zassert_equal(0, data_logger_block_write(logger, 4, input_buffer, state.block_size));

	/* Set a bad label on the filesystem */
	zassert_equal(FR_OK, f_setlabel(bad_label));
	/* Re-init the filesystem */
	zassert_equal(0, data_logger_init(logger));
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
	zassert_equal(0, data_logger_init(logger));
	data_logger_get_state(logger, &state);

#ifdef CONFIG_DISK_DRIVER_SDMMC
	uint32_t max_blocks = 50;
#else
	/* We lose an unpredicable number of blocks to file allocation tables.
	 * Actual loss depends on the size of binary files vs partition size.
	 * Treat 95% storage as a pass.
	 */
	uint32_t max_blocks = 95 * state.physical_blocks / 100;
	uint32_t overhead_blocks = 5 * state.physical_blocks / 100;
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
			zassert_equal(0, data_logger_init(logger));
			data_logger_get_state(logger, &state);
			zassert_equal(i + 1, state.current_block);
		}
	}

#ifndef CONFIG_DISK_DRIVER_SDMMC
	int rc = 0;

	/* Somewhere in here we should get a write error */
	for (int i = 0; i < overhead_blocks; i++) {
		rc = data_logger_block_write(logger, 5, input_buffer, state.block_size);
		data_logger_get_state(logger, &state);

		/* We expect an expand to fail (-ENOMEM) followed by failures to write (-ENOMEM) */
		if (rc == -ENOMEM) {
			uint32_t failing_block = state.current_block;

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
#endif
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

static bool test_data_init(const void *global_state)
{
	memory = flash_simulator_get_memory(DEVICE_DT_GET_ONE(zephyr_sim_flash), &memory_size);
	return true;
}

static void partition_wipe(void *fixture)
{
	const struct flash_parameters *params =
		flash_get_parameters(DEVICE_DT_GET_ONE(zephyr_sim_flash));

	memset(memory, params->erase_value, memory_size);
}

ZTEST_SUITE(data_logger_exfat, test_data_init, NULL, partition_wipe, NULL, NULL);
