/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

enum {
	TDF_RANDOM = 37,
};

static uint8_t *flash_buffer;
static size_t flash_buffer_size;

int tdf_data_logger_init(const struct device *dev);
int logger_flash_map_init(const struct device *dev);

ZTEST(tdf_data_logger_flash, test_standard)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	uint8_t tdf_data[32];
	int rc;

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
}

ZTEST(tdf_data_logger_flash, test_multi)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	uint8_t tdf_data[192] = {0};
	int rc;

	/* 156 bytes (6 overhead, 25 * 6 data) */
	rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_RANDOM, 6, 25, 0, 0, tdf_data);
	zassert_equal(0, rc);
	/* 156 bytes (6 overhead, 25 * 6 data) */
	rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_RANDOM, 6, 25, 0, 0, tdf_data);
	zassert_equal(0, rc);
	/* 156 bytes (6 overhead, 25 * 6 data) */
	rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_RANDOM, 6, 25, 0, 0, tdf_data);
	zassert_equal(0, rc);

	data_logger_get_state(data_logger, &state);
	zassert_equal(0, state.current_block);

	/* 156 bytes (6 overhead, 25 * 6 data) */
	rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_RANDOM, 6, 25, 0, 0, tdf_data);
	zassert_equal(0, rc);

	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);
}

static void log_400(const struct device *tdf_logger)
{
	uint8_t tdf_data[100] = {0};
	int rc;

	for (int i = 0; i < 4; i++) {
		rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 97, 0, tdf_data);
		zassert_equal(0, rc);
	}
}

ZTEST(tdf_data_logger_flash, test_auto_flush)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	struct data_logger_state state;
	uint8_t tdf_data[128];
	int rc;

	/* Initial 400 bytes */
	log_400(tdf_logger);

	/* Up to 506 bytes should not flush */
	rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 103, 0, tdf_data);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(0, state.current_block);

	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);

	/* 507 through 510 should auto flush */
	for (int i = 0; i < 4; i++) {
		log_400(tdf_logger);

		rc = tdf_data_logger_log_dev(tdf_logger, TDF_RANDOM, 104 + i, 0, tdf_data);
		zassert_equal(0, rc);
		data_logger_get_state(data_logger, &state);
		zassert_equal(2 + i, state.current_block);
	}
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

ZTEST_SUITE(tdf_data_logger_flash, test_data_init, NULL, data_logger_reset, NULL, NULL);
