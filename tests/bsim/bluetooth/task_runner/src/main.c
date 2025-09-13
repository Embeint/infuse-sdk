/**
 * Copyright (c) 2025 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/time/epoch.h>
#include <infuse/states.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/bt_scanner.h>

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, "PASSED: " __VA_ARGS__);                                     \
	} while (0)

#define WAIT_SECONDS 30                            /* seconds */
#define WAIT_TIME    (WAIT_SECONDS * USEC_PER_SEC) /* microseconds*/

extern enum bst_result_t bst_result;

struct task_schedule schedules[1] = {
	{
		.validity = TASK_VALID_ALWAYS,
		.task_id = TASK_ID_BT_SCANNER,
		/* Run once at start */
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = TASK_RUNNER_LOCKOUT_IGNORE_FIRST | 60,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_UDP,
					.tdf_mask = TASK_BT_SCANNER_LOG_INFUSE_BT,
				},
			},
	},
};
struct task_schedule_state states[ARRAY_SIZE(schedules)];
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (BT_SCANNER_TASK));

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void task_run(void)
{
	INFUSE_STATES_ARRAY(infuse_states);

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	for (int i = 0; i < 9; i++) {
		int64_t uptime_ticks = k_uptime_ticks();
		uint32_t uptime_sec = k_ticks_to_sec_floor32(uptime_ticks);
		uint32_t gps_time = epoch_time_seconds(epoch_time_now());

		/* Iterate the runner */
		infuse_states_snapshot(infuse_states);
		task_runner_iterate(infuse_states, uptime_sec, gps_time, 100);
		infuse_states_tick(infuse_states);

		k_sleep(K_TIMEOUT_ABS_SEC(uptime_sec + 1));
	}

	/* Expect task to be stopped */
	if (k_work_delayable_busy_get(&app_tasks_data[0].executor.workqueue.work)) {
		FAIL("Task still running\n");
	}
}

static void main_bt_scanner_timeout(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;
	int logged = 0;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	while (tdf_parse(&state, &parsed) == 0) {
		if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) || (parsed.tdf_num != 1) ||
		    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) ||
		    (parsed.time == 0)) {
			FAIL("Unexpected TDF data\n");
		}
		logged += 1;
	}
	net_buf_unref(buf);

	if (logged < 12) {
		FAIL("Not enough TDFs observed\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_self_duration(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;
	int logged = 0;

	/* Set this tests parameters and run the task */
	schedules[0].task_args.infuse.bt_scanner.duration_ms = 5000;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	while (tdf_parse(&state, &parsed) == 0) {
		if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) || (parsed.tdf_num != 1) ||
		    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) ||
		    (parsed.time == 0)) {
			FAIL("Unexpected TDF data\n");
		}
		logged += 1;
	}
	net_buf_unref(buf);

	if (logged < 12) {
		FAIL("Not enough TDFs observed\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_scan_4(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;
	int logged = 0;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.max_logs = 4;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	while (tdf_parse(&state, &parsed) == 0) {
		if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) || (parsed.tdf_num != 1) ||
		    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) ||
		    (parsed.time == 0)) {
			FAIL("Unexpected TDF data\n");
		}
		logged += 1;
	}
	net_buf_unref(buf);

	if (logged != 4) {
		FAIL("Unexpected number of TDFs observed\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_filter_duplicates(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;
	int logged = 0;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.flags = TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	while (tdf_parse(&state, &parsed) == 0) {
		if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) || (parsed.tdf_num != 1) ||
		    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) ||
		    (parsed.time == 0)) {
			FAIL("Unexpected TDF data\n");
		}
		logged += 1;
	}
	net_buf_unref(buf);

	if (logged != 5) {
		FAIL("Unexpected number of TDFs observed\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_defer_logging(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.flags = TASK_BT_SCANNER_FLAGS_DEFER_LOGGING;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	/* Expect a single TDF with many elements */
	if (tdf_parse(&state, &parsed) != 0) {
		FAIL("Unexpected number of TDFs observed");
		return;
	}
	if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) ||
	    (parsed.data_type != TDF_DATA_FORMAT_TIME_ARRAY) ||
	    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) || (parsed.time == 0)) {
		FAIL("Unexpected TDF data\n");
	}
	net_buf_unref(buf);

	if (parsed.tdf_num != CONFIG_TASK_RUNNER_TASK_BT_SCANNER_MAX_DEVICES) {
		FAIL("Unexpected number of TDFs in array\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_defer_filter(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.flags =
		TASK_BT_SCANNER_FLAGS_DEFER_LOGGING | TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	/* Expect a single TDF with many elements */
	if (tdf_parse(&state, &parsed) != 0) {
		FAIL("Unexpected number of TDFs observed");
		return;
	}
	if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) ||
	    (parsed.data_type != TDF_DATA_FORMAT_TIME_ARRAY) ||
	    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) || (parsed.time == 0)) {
		FAIL("Unexpected TDF data\n");
	}
	net_buf_unref(buf);

	if (parsed.tdf_num != 5) {
		FAIL("Unexpected number of TDFs in array\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_defer_filter_limit(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.max_logs = 3;
	schedules[0].task_args.infuse.bt_scanner.flags =
		TASK_BT_SCANNER_FLAGS_DEFER_LOGGING | TASK_BT_SCANNER_FLAGS_FILTER_DUPLICATES;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	/* Expect a single TDF with many elements */
	if (tdf_parse(&state, &parsed) != 0) {
		FAIL("Unexpected number of TDFs observed");
		return;
	}
	if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) ||
	    (parsed.data_type != TDF_DATA_FORMAT_TIME_ARRAY) ||
	    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) || (parsed.time == 0)) {
		FAIL("Unexpected TDF data\n");
	}
	net_buf_unref(buf);

	if (parsed.tdf_num != 3) {
		FAIL("Unexpected number of TDFs in array\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_defer_no_logs(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.flags = TASK_BT_SCANNER_FLAGS_DEFER_LOGGING;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf != NULL) {
		FAIL("Unexpected TDFs\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_encrypted_log(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_buffer_state state;
	struct tdf_parsed parsed;
	struct net_buf *buf;
	int logged = 0;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	schedules[0].task_args.infuse.bt_scanner.flags = TASK_BT_SCANNER_FLAGS_LOG_ENCRYPTED;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf == NULL) {
		FAIL("No TDFs logged\n");
		return;
	}
	net_buf_pull(buf, sizeof(struct epacket_dummy_frame));

	/* Validate the logged data */
	tdf_parse_start(&state, buf->data, buf->len);
	while (tdf_parse(&state, &parsed) == 0) {
		if ((parsed.tdf_id != TDF_INFUSE_BLUETOOTH_RSSI) || (parsed.tdf_num != 1) ||
		    (parsed.tdf_len != sizeof(struct tdf_infuse_bluetooth_rssi)) ||
		    (parsed.time == 0)) {
			FAIL("Unexpected TDF data\n");
		}
		logged += 1;
	}
	net_buf_unref(buf);

	if (logged < 4) {
		FAIL("Not enough TDFs observed\n");
	} else {
		PASS("Task runner complete\n");
	}
}

static void main_bt_scanner_encrypted_skip(void)
{
	struct k_fifo *sent_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *buf;

	/* Set this tests parameters and run the task */
	schedules[0].timeout_s = 5;
	task_run();

	/* Flush all logged data and get the packet */
	tdf_data_logger_flush(TDF_DATA_LOGGER_UDP);
	buf = k_fifo_get(sent_queue, K_MSEC(10));
	if (buf != NULL) {
		FAIL("Unexpected TDFs\n");
	} else {
		PASS("Task runner complete\n");
	}
}

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i seconds)\n", WAIT_SECONDS);
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
	bst_result = In_progress;
}

static const struct bst_test_instance ext_adv_advertiser[] = {
	{
		.test_id = "bt_scanner_timeout",
		.test_descr = "Scan with no restrictions until timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_timeout,
	},
	{
		.test_id = "bt_scanner_self_duration",
		.test_descr = "Scan with no restrictions until self timeout",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_self_duration,
	},
	{
		.test_id = "bt_scanner_scan_4",
		.test_descr = "Scan until 4 packets are found",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_scan_4,
	},
	{
		.test_id = "bt_scanner_filter_duplicates",
		.test_descr = "Scan with duplicate filtering",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_filter_duplicates,
	},
	{
		.test_id = "bt_scanner_defer_logging",
		.test_descr = "Scan with logging deferred until the end",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_defer_logging,
	},
	{
		.test_id = "bt_scanner_defer_filter",
		.test_descr = "Scan with logging deferred and duplicate filtering",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_defer_filter,
	},
	{
		.test_id = "bt_scanner_defer_filter_limit",
		.test_descr = "Scan with logging deferred, duplicate filtering and limit",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_defer_filter_limit,
	},
	{
		.test_id = "bt_scanner_defer_no_logs",
		.test_descr = "Scan with logging deferred, no devices observed",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_defer_no_logs,
	},
	{
		.test_id = "bt_scanner_encrypted_log",
		.test_descr = "Scan with logging of still-encrypted packets",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_encrypted_log,
	},
	{
		.test_id = "bt_scanner_encrypted_skip",
		.test_descr = "Scan while skipping still-encrypted packets",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_bt_scanner_encrypted_skip,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_ext_adv_advertiser(struct bst_test_list *tests)
{
	return bst_add_tests(tests, ext_adv_advertiser);
}

bst_test_install_t test_installers[] = {test_ext_adv_advertiser, NULL};

int main(void)
{
	bst_main();
	return 0;
}
