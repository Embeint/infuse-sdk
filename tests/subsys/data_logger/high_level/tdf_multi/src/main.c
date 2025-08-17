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

#include <infuse/epacket/packet.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

enum {
	TDF_RANDOM = 37,
};

static void validate_loggers(uint32_t expected_flash, uint32_t expected_epacket)
{
	const struct device *flash_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_flash));
	const struct device *epacket_logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_udp));
	struct data_logger_state state;

	data_logger_get_state(flash_logger, &state);
	zassert_equal(expected_flash, state.current_block);
	data_logger_get_state(epacket_logger, &state);
	zassert_equal(expected_epacket, state.current_block);
}

ZTEST(tdf_data_logger_multi, test_standard)
{
	uint8_t tdf_data[32];
	uint32_t expected_flash = 0;
	uint32_t expected_epacket = 0;

	/* Initial state */
	validate_loggers(expected_flash, expected_epacket);

	/* Flush both devices */
	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP);
	validate_loggers(expected_flash, expected_epacket);

	/* Push data to one, flush the other */
	tdf_data_logger_log(TDF_DATA_LOGGER_FLASH, TDF_RANDOM, 17, 0, tdf_data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);

	validate_loggers(expected_flash, expected_epacket);

	/* Flush the one we pushed to */
	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH);
	validate_loggers(++expected_flash, expected_epacket);

	/* Push to non-existent */
	tdf_data_logger_log(0x80 | TDF_DATA_LOGGER_SERIAL, TDF_RANDOM, 17, 0, tdf_data);
	tdf_data_logger_flush(0x80 | TDF_DATA_LOGGER_SERIAL);
	validate_loggers(expected_flash, expected_epacket);

	/* Add to both */
	tdf_data_logger_log(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP, TDF_RANDOM, 17, 0,
			    tdf_data);
	validate_loggers(expected_flash, expected_epacket);

	/* Flush both */
	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP);
	validate_loggers(++expected_flash, ++expected_epacket);

	/* Test the type safe macros */
	TDF_TYPE(TDF_ACC_2G) acc = {{1, 2, 3}};
	TDF_TYPE(TDF_GYR_125DPS) gyr[2] = {{{-1, -2, -3}}, {{4, 5, 6}}};

	TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP, TDF_ACC_2G, 0, &acc);
	TDF_DATA_LOGGER_LOG_ARRAY(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP, TDF_GYR_125DPS, 2, 0,
				  10, gyr);

	/* Flush both devices */
	tdf_data_logger_flush(TDF_DATA_LOGGER_FLASH | TDF_DATA_LOGGER_UDP);
	validate_loggers(++expected_flash, ++expected_epacket);
}

ZTEST_SUITE(tdf_data_logger_multi, NULL, NULL, NULL, NULL, NULL);
