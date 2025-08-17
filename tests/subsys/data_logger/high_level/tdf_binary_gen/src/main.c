/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/random/random.h>

#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>

/* The purpose of this test is to generate a .bin file that can be used to validate decoders */
ZTEST(tdf_data_logger_flash, test_standard)
{
	const struct device *tdf_logger = DEVICE_DT_GET(DT_NODELABEL(tdf_logger_flash));
	const struct device *data_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	uint64_t time_now = epoch_time_from_gps(2347, 259218, 0);
	uint32_t sample_period, buffer_period;
	struct tdf_acc_4g acc_array[32];
	struct tdf_ambient_temperature ambient_array[8];
	struct tdf_idx_array_freq idx_array_info;
	struct data_logger_state state;
	int rc;
	int i;

	/* Initialise sample arrays */
	for (i = 0; i < ARRAY_SIZE(acc_array); i++) {
		acc_array[i].sample.x = 8192 + (int8_t)sys_rand8_get();
		acc_array[i].sample.y = -1024 - i;
		acc_array[i].sample.z = 1024 + 2 * i;
	}
	for (i = 0; i < ARRAY_SIZE(ambient_array); i++) {
		ambient_array[i].temperature = 27000 + (100 * i);
	}

	/* Simulate 100hz data */
	sample_period = INFUSE_EPOCH_TIME_TICKS_PER_SEC / 100;
	buffer_period = (ARRAY_SIZE(acc_array) * INFUSE_EPOCH_TIME_TICKS_PER_SEC) / 100;

	/* Log 2 seconds of data at 100 Hz as individual samples */
	for (i = 0; i < 200; i++) {
		rc = tdf_data_logger_log_dev(tdf_logger, TDF_ACC_4G, sizeof(struct tdf_acc_4g),
					     time_now, &acc_array[i % ARRAY_SIZE(acc_array)]);
		zassert_equal(0, rc);
		time_now += sample_period;
	}

	/* Log data with a long period */
	rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_AMBIENT_TEMPERATURE,
					   sizeof(struct tdf_ambient_temperature),
					   ARRAY_SIZE(ambient_array), time_now,
					   INFUSE_EPOCH_TIME_TICKS_PER_SEC, ambient_array);
	zassert_equal(0, rc);

	/* Log 2 seconds of data at 100 Hz as a time array */
	i = 0;
	while (i < 200) {
		rc = tdf_data_logger_log_array_dev(tdf_logger, TDF_ACC_4G,
						   sizeof(struct tdf_acc_4g), ARRAY_SIZE(acc_array),
						   time_now, sample_period, acc_array);
		zassert_equal(0, rc);
		time_now += buffer_period;
		i += ARRAY_SIZE(acc_array);
	}

	/* Log data with the TDF_DATA_FORMAT_DIFF_ARRAY_32_8 type */
	rc = tdf_data_logger_log_core_dev(
		tdf_logger, TDF_AMBIENT_TEMPERATURE, sizeof(struct tdf_ambient_temperature),
		ARRAY_SIZE(ambient_array), TDF_DATA_FORMAT_DIFF_ARRAY_32_8, time_now,
		INFUSE_EPOCH_TIME_TICKS_PER_SEC, ambient_array);
	zassert_equal(0, rc);

	/* Log 2 seconds of data at 100 Hz as a diff array */
	i = 0;
	while (i < 200) {
		rc = tdf_data_logger_log_core_dev(
			tdf_logger, TDF_ACC_4G, sizeof(struct tdf_acc_4g), ARRAY_SIZE(acc_array),
			TDF_DATA_FORMAT_DIFF_ARRAY_16_8, time_now, sample_period, acc_array);
		zassert_equal(0, rc);
		time_now += buffer_period;
		i += ARRAY_SIZE(acc_array);
	}

	/* Log data with the TDF_DATA_FORMAT_DIFF_ARRAY_32_16 type */
	rc = tdf_data_logger_log_core_dev(
		tdf_logger, TDF_AMBIENT_TEMPERATURE, sizeof(struct tdf_ambient_temperature),
		ARRAY_SIZE(ambient_array), TDF_DATA_FORMAT_DIFF_ARRAY_32_16, time_now,
		INFUSE_EPOCH_TIME_TICKS_PER_SEC, ambient_array);
	zassert_equal(0, rc);

	/* Log 2 seconds of data at 100 Hz as an index array (and metadata) */
	idx_array_info.tdf_id = TDF_ACC_4G;
	idx_array_info.frequency = 100;
	rc = tdf_data_logger_log_dev(tdf_logger, TDF_IDX_ARRAY_FREQ, sizeof(idx_array_info),
				     time_now, &idx_array_info);
	zassert_equal(0, rc);
	i = 0;
	while (i < 200) {
		rc = tdf_data_logger_log_core_dev(tdf_logger, TDF_ACC_4G, sizeof(struct tdf_acc_4g),
						  ARRAY_SIZE(acc_array), TDF_DATA_FORMAT_IDX_ARRAY,
						  time_now, i, acc_array);
		zassert_equal(0, rc);
		time_now = 0;
		i += ARRAY_SIZE(acc_array);
	}

	/* Log one more with index rollover */
	rc = tdf_data_logger_log_core_dev(tdf_logger, TDF_ACC_4G, sizeof(struct tdf_acc_4g),
					  ARRAY_SIZE(acc_array), TDF_DATA_FORMAT_IDX_ARRAY,
					  time_now, UINT16_MAX - 4, acc_array);
	zassert_equal(0, rc);

	/* Flush remaining data to disk */
	rc = tdf_data_logger_flush_dev(tdf_logger);
	zassert_equal(0, rc);

	/* Ensure we haven't overwritten data */
	data_logger_get_state(data_logger, &state);
	printk("Logged to %d/%d blocks\n", state.current_block, state.physical_blocks);
	zassert_true(state.current_block < state.physical_blocks);
}

static bool test_flash_erase(const void *global_state)
{
	const struct flash_area *area;

	/* Erase flash at start of test */
	flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(storage)), &area);
	flash_area_erase(area, 0, area->fa_size);
	flash_area_close(area);
	return true;
}

ZTEST_SUITE(tdf_data_logger_flash, test_flash_erase, NULL, NULL, NULL, NULL);
