/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

enum {
	TDF_RANDOM = 37,
};

static uint8_t *flash_buffer;
static size_t flash_buffer_size;
static uint8_t block_buffer[512];

int tdf_data_logger_init(const struct device *dev);
int logger_flash_map_init(const struct device *dev);

static void validate_tdf_remote(const struct device *dev, uint32_t block, uint64_t expected_id)
{
	zassert_equal(0, data_logger_block_read(dev, block, 0, block_buffer, sizeof(block_buffer)));
	zassert_equal(INFUSE_TDF_REMOTE, block_buffer[1]);
	zassert_equal(expected_id, sys_get_le64(block_buffer + 2));
}

ZTEST(tdf_data_logger_remote, test_non_remote)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_other));

	zassert_equal(-EINVAL, tdf_data_logger_remote_id_set(tdf_logger, 0x800012345678ABCD));
}

ZTEST(tdf_data_logger_remote, test_standard)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	uint8_t tdf_data[32];
	int rc;

	rc = tdf_data_logger_remote_id_set(tdf_logger, 0x800012345678ABCD);
	zassert_equal(0, rc);

	/* 20 bytes per log (3 overhead, 17 data) = 160 bytes */
	for (int i = 0; i < 8; i++) {
		rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 17, 0, tdf_data);
		zassert_equal(0, rc);
	}
	data_logger_get_state(data_logger, &state);
	zassert_equal(0, state.current_block);

	/* Flush logger */
	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);

	/* Expected ID */
	validate_tdf_remote(data_logger, 0, 0x800012345678ABCD);

	/* Flushing here shouldn't do anything */
	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);
}

ZTEST(tdf_data_logger_remote, test_id_change)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	uint8_t tdf_data[32];
	int rc;

	rc = tdf_data_logger_remote_id_set(tdf_logger, 0x800012345678ABCD);
	zassert_equal(0, rc);

	/* Log some data, then set the ID to the same */
	rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 17, 0, tdf_data);
	zassert_equal(0, rc);
	rc = tdf_data_logger_remote_id_set(tdf_logger, 0x800012345678ABCD);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(0, state.current_block);

	/* Change the ID, data should now flush */
	rc = tdf_data_logger_remote_id_set(tdf_logger, 0x12345678ABC0AAAA);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);
	validate_tdf_remote(data_logger, 0, 0x800012345678ABCD);

	/* Log more data and flush, ID on the block should be the new value */
	rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 17, 0, tdf_data);
	zassert_equal(0, rc);
	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(2, state.current_block);
	validate_tdf_remote(data_logger, 1, 0x12345678ABC0AAAA);
}

void data_logger_reset(void *fixture)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));

	/* Clear any pending data */
	tdf_data_logger_flush_dev(tdf_logger);

	/* Erase amd reinitialise loggers */
	memset(flash_buffer, 0xFF, flash_buffer_size);
	logger_flash_map_init(data_logger);
	tdf_data_logger_init(tdf_logger);
}

static bool test_data_init(const void *global_state)
{
	flash_buffer = flash_simulator_get_memory(DEVICE_DT_GET(DT_NODELABEL(sim_flash)),
						  &flash_buffer_size);
	return true;
}

ZTEST_SUITE(tdf_data_logger_remote, test_data_init, NULL, data_logger_reset, NULL, NULL);
