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
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_fuel_gauge.h>

#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/battery.h>

#define DEV      DEVICE_DT_GET_ANY(sbs_sbs_gauge_new_api)
#define EMUL_DEV EMUL_DT_GET(DT_NODELABEL(smartbattery0))
BATTERY_TASK(1, 0, DEV);
struct task_config config = BATTERY_TASK(0, 1, DEV);
struct task_data data;
struct task_schedule schedule;
struct task_schedule_state state;

static K_SEM_DEFINE(bat_published, 0, 1);

static void bat_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_BATTERY);
ZBUS_LISTENER_DEFINE(bat_listener, bat_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_BATTERY), bat_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

static void bat_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&bat_published);
}

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void expect_logging(uint8_t log_mask, uint32_t battery_uv, int32_t current_ua, uint8_t soc)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct tdf_parsed tdf;
	struct net_buf *pkt;
	int rc;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (log_mask == 0) {
		zassert_is_null(pkt);
		return;
	}

	zassert_not_null(pkt);

	net_buf_pull(pkt, sizeof(struct epacket_dummy_frame));

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_BATTERY_STATE, &tdf);
	if (log_mask & TASK_BATTERY_LOG_COMPLETE) {
		struct tdf_battery_state *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(battery_uv / 1000, state->voltage_mv);
		zassert_equal(current_ua, state->current_ua);
		zassert_equal(soc, state->soc);
	} else {
		zassert_equal(-ENOMEM, rc);
	}
	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_BATTERY_VOLTAGE, &tdf);
	if (log_mask & TASK_BATTERY_LOG_VOLTAGE) {
		struct tdf_battery_voltage *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(battery_uv / 1000, state->voltage);
	} else {
		zassert_equal(-ENOMEM, rc);
	}
	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_BATTERY_SOC, &tdf);
	if (log_mask & TASK_BATTERY_LOG_SOC) {
		struct tdf_battery_soc *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(soc, state->soc);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

static void test_battery(uint32_t battery_uv, int32_t current_ua, uint8_t log_mask)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_BATTERY) battery_reading;
	uint32_t pub_count;

	schedule.task_logging[0].tdf_mask = log_mask;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	/* Configure emulator */
	emul_fuel_gauge_set_battery_charging(EMUL_DEV, battery_uv, current_ua);

	/* Clear state */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	(void)k_sem_take(&bat_published, K_NO_WAIT);

	/* Schedule task */
	task_schedule(&data);

	k_sleep(K_MSEC(500));

	/* Task should be complete */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));
	zassert_equal(pub_count + 1, zbus_chan_pub_stats_count(ZBUS_CHAN));
	zbus_chan_read(ZBUS_CHAN, &battery_reading, K_FOREVER);
	zassert_equal(battery_uv / 1000, battery_reading.voltage_mv);
	zassert_equal(current_ua, battery_reading.current_ua);
	zassert_equal(1, battery_reading.soc);

	expect_logging(log_mask, battery_uv, current_ua, 1);
}

ZTEST(task_bat, test_no_log)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	test_battery(3700000, 10000, 0);
	test_battery(3501000, -15000, 0);
}

ZTEST(task_bat, test_log)
{
	uint8_t log_all =
		TASK_BATTERY_LOG_COMPLETE | TASK_BATTERY_LOG_VOLTAGE | TASK_BATTERY_LOG_SOC;

	schedule = (struct task_schedule){
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	test_battery(3700000, 10000, TASK_BATTERY_LOG_COMPLETE);
	test_battery(3501000, -15000, TASK_BATTERY_LOG_COMPLETE);

	test_battery(4200000, 18000, TASK_BATTERY_LOG_VOLTAGE);
	test_battery(4201000, -7000, TASK_BATTERY_LOG_VOLTAGE);

	test_battery(3600000, 15000, log_all);
	test_battery(3601000, -10000, log_all);
}

ZTEST(task_bat, test_periodic)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t base_count, pub_count;

	schedule = (struct task_schedule){
		.task_id = TASK_ID_BATTERY,
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
		.timeout_s = 5,
		.task_args.infuse.battery =
			{
				.repeat_interval_ms = 990,
			},
	};

	/* Configure emulator */
	emul_fuel_gauge_set_battery_charging(EMUL_DEV, 3700000, 1000);

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Get initial count */
	base_count = zbus_chan_pub_stats_count(ZBUS_CHAN);

	/* Iterate for 7 seconds */
	for (int i = 0; i < 8; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_SECONDS(1));
	}

	/* Task should no longer be running (terminated by runner on timeout) */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));

	/* Expect 6 publishes over the time period (1 at start, 5 rescheduled before timeout) */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	zassert_equal(base_count + 6, pub_count);
}

static void logger_before(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (pkt) {
		net_buf_unref(pkt);
	}
	k_sem_reset(&bat_published);
}

ZTEST_SUITE(task_bat, NULL, NULL, logger_before, NULL, NULL);
