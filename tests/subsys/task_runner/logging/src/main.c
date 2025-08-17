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
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

#include <infuse/task_runner/task.h>

enum {
	TDF_RANDOM = 37,
};

enum {
	TASK_LOG_TDF_0 = BIT(0),
	TASK_LOG_TDF_1 = BIT(1),
	TASK_LOG_TDF_2 = BIT(2),
	TASK_LOG_TDF_3 = BIT(3),
	TASK_LOG_TDF_4 = BIT(4),
};

ZTEST(task_runner_logging, test_tdf_requested)
{

	struct task_schedule schedule = {
		.task_logging = {
			{
				.loggers = TDF_DATA_LOGGER_SERIAL | TDF_DATA_LOGGER_BT_ADV,
				.tdf_mask = TASK_LOG_TDF_1 | TASK_LOG_TDF_4,
			},
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdf_mask = TASK_LOG_TDF_2 | TASK_LOG_TDF_4,
			},
		}};

	zassert_false(task_schedule_tdf_requested(&schedule, TASK_LOG_TDF_0));
	zassert_true(task_schedule_tdf_requested(&schedule, TASK_LOG_TDF_1));
	zassert_true(task_schedule_tdf_requested(&schedule, TASK_LOG_TDF_2));
	zassert_false(task_schedule_tdf_requested(&schedule, TASK_LOG_TDF_3));
	zassert_true(task_schedule_tdf_requested(&schedule, TASK_LOG_TDF_4));
}

ZTEST(task_runner_logging, test_tdf_logging)
{
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct net_buf *tx;
	uint32_t tdf_data = 0x12349876;

	/* TDF is on first slot */
	struct task_schedule schedule1 = {
		.task_logging = {
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.tdf_mask = TASK_LOG_TDF_1 | TASK_LOG_TDF_4,
			},
		}};
	/* TDF is on second slot */
	struct task_schedule schedule2 = {
		.task_logging = {
			{
				.loggers = TDF_DATA_LOGGER_UDP,
				.tdf_mask = TASK_LOG_TDF_1 | TASK_LOG_TDF_4,
			},
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.tdf_mask = TASK_LOG_TDF_3,
			},
		}};

	/* TASK_LOG_TDF_0 is not requested */
	task_schedule_tdf_log(&schedule1, TASK_LOG_TDF_0, TDF_RANDOM, sizeof(tdf_data), 0,
			      &tdf_data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);

	/* TASK_LOG_TDF_1 is requested */
	task_schedule_tdf_log(&schedule1, TASK_LOG_TDF_1, TDF_RANDOM, sizeof(tdf_data), 0,
			      &tdf_data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_unref(tx);

	/* TASK_LOG_TDF_2 is not requested */
	task_schedule_tdf_log(&schedule2, TASK_LOG_TDF_2, TDF_RANDOM, sizeof(tdf_data), 0,
			      &tdf_data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_is_null(tx);

	/* TASK_LOG_TDF_3 is requested */
	task_schedule_tdf_log(&schedule2, TASK_LOG_TDF_3, TDF_RANDOM, sizeof(tdf_data), 0,
			      &tdf_data);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_unref(tx);
}

ZTEST(task_runner_logging, test_tdf_type_safe)
{
	TDF_TYPE(TDF_ACC_2G) readings[2] = {{{1, 2, 3}}, {{-4, -5, -6}}};
	struct k_fifo *tx_fifo = epacket_dummmy_transmit_fifo_get();
	struct net_buf *tx;

	/* TDF is on first slot */
	struct task_schedule schedule1 = {
		.task_logging = {
			{
				.loggers = TDF_DATA_LOGGER_SERIAL,
				.tdf_mask = TASK_LOG_TDF_1 | TASK_LOG_TDF_4,
			},
		}};

	TASK_SCHEDULE_TDF_LOG(&schedule1, TASK_LOG_TDF_1, TDF_ACC_2G, 0, readings);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_unref(tx);

	TASK_SCHEDULE_TDF_LOG_ARRAY(&schedule1, TASK_LOG_TDF_1, TDF_ACC_2G, 2, 0, 10, readings);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	tx = k_fifo_get(tx_fifo, K_MSEC(100));
	zassert_not_null(tx);
	net_buf_unref(tx);
}

ZTEST_SUITE(task_runner_logging, NULL, NULL, NULL, NULL, NULL);
