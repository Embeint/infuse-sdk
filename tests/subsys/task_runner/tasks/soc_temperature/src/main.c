/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>

#include <infuse/drivers/sensor/generic_sim.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/data_logger/logger.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/states.h>
#include <infuse/tdf/tdf.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/task.h>
#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/soc_temperature.h>

#define DIE_TEMP DEVICE_DT_GET(DT_NODELABEL(sim_die_temp))
SOC_TEMPERATURE_TASK(1, 0, DIE_TEMP);
struct task_config config = SOC_TEMPERATURE_TASK(0, 1, DIE_TEMP);

struct task_data data;
struct task_schedule schedule;
struct task_schedule_state state;

static K_SEM_DEFINE(soc_temp_published, 0, 1);

static void soc_temp_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE);
ZBUS_LISTENER_DEFINE(soc_temp_listener, soc_temp_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE), soc_temp_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE)

static void soc_temp_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&soc_temp_published);
}

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void expect_logging(uint8_t log_mask, int32_t temperature)
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

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_SOC_TEMPERATURE, &tdf);
	if (log_mask & TASK_SOC_TEMPERATURE_LOG_T) {
		struct tdf_soc_temperature *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(temperature / 10, state->temperature);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

static void test_soc_temperature(int32_t temperature, uint8_t log_mask)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_SOC_TEMPERATURE) temp_reading;
	struct sensor_value value;
	uint32_t pub_count;

	/* Reset all channel info */
	generic_sim_reset(DIE_TEMP, false);

	schedule.task_logging[0].tdf_mask = log_mask;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	sensor_value_from_milli(&value, temperature);
	generic_sim_channel_set(DIE_TEMP, SENSOR_CHAN_DIE_TEMP, value);

	/* Clear state */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	(void)k_sem_take(&soc_temp_published, K_NO_WAIT);

	/* Schedule task */
	task_schedule(&data);

	k_sleep(K_MSEC(500));

	/* Task should be complete */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));
	zassert_equal(pub_count + 1, zbus_chan_pub_stats_count(ZBUS_CHAN));
	zbus_chan_read(ZBUS_CHAN, &temp_reading, K_FOREVER);

	zassert_equal(temperature / 10, temp_reading.temperature);

	expect_logging(log_mask, temperature);
}

static void test_cfg(int32_t temperature)
{
	test_soc_temperature(temperature, 0);
	test_soc_temperature(temperature, TASK_SOC_TEMPERATURE_LOG_T);
}

ZTEST(task_soc_temperature, test_temperature_single)
{
	schedule = (struct task_schedule){
		.task_id = TASK_ID_SOC_TEMPERATURE,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	test_cfg(-11000);
	test_cfg(23000);
	test_cfg(79272);
}

ZTEST(task_soc_temperature, test_failures)
{
	uint32_t pub_count;

	schedule = (struct task_schedule){
		.task_id = TASK_ID_SOC_TEMPERATURE,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Fetch fails */
	generic_sim_func_rc(DIE_TEMP, 0, 0, -EIO);

	/* No TDF logging or ZBUS publishing */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	zassert_equal(pub_count, zbus_chan_pub_stats_count(ZBUS_CHAN));
	expect_logging(0, 0);
}

static void logger_before(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	generic_sim_reset(DIE_TEMP, true);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (pkt) {
		net_buf_unref(pkt);
	}
	k_sem_reset(&soc_temp_published);
}

ZTEST_SUITE(task_soc_temperature, NULL, NULL, logger_before, NULL, NULL);
