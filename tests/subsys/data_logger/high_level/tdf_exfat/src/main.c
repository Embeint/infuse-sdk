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
#include <zephyr/storage/disk_access.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>

#include <ff.h>

enum {
	TDF_RANDOM = 37,
};

ZTEST(tdf_data_logger_exfat, test_block_padding)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_exfat));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_exfat));
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

	/* Flush logger (Data should be padded and not assert) */
	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);
	data_logger_get_state(data_logger, &state);
	zassert_equal(1, state.current_block);
}

ZTEST_SUITE(tdf_data_logger_exfat, NULL, NULL, NULL, NULL, NULL);
