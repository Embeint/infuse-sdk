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
#include <infuse/task_runner/tasks/environmental.h>

#define ENV0 DEVICE_DT_GET(DT_NODELABEL(sim_env0))
#define ENV1 DEVICE_DT_GET(DT_NODELABEL(sim_env1))
ENVIRONMENTAL_TASK(1, 0, ENV0, NULL);
struct task_config config = ENVIRONMENTAL_TASK(0, 1, ENV0, NULL);
const struct task_environmental_devices env_task_devices_dual = {
	.primary = ENV0,
	.secondary = ENV1,
};
struct task_config config_dual = {
	.name = "env",
	.task_id = TASK_ID_ENVIRONMENTAL,
	.exec_type = TASK_EXECUTOR_WORKQUEUE,
	.task_arg.const_arg = &env_task_devices_dual,
	.executor.workqueue =
		{
			.worker_fn = environmental_task_fn,
		},
};
struct task_data data;
struct task_schedule schedule;
struct task_schedule_state state;

struct test_configuration {
	struct {
		int32_t temperature;
		uint32_t pressure;
		uint32_t humidity;
	} primary;
	struct {
		int32_t temperature;
		uint32_t pressure;
		uint32_t humidity;
	} secondary;
	struct {
		int32_t temperature;
		uint32_t pressure;
		uint32_t humidity;
	} output;
};

static K_SEM_DEFINE(env_published, 0, 1);

static void env_new_data_cb(const struct zbus_channel *chan);

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_AMBIENT_ENV);
ZBUS_LISTENER_DEFINE(env_listener, env_new_data_cb);
ZBUS_CHAN_ADD_OBS(INFUSE_ZBUS_NAME(INFUSE_ZBUS_CHAN_AMBIENT_ENV), env_listener, 5);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_AMBIENT_ENV)

static void expected_pm_state(const struct device *dev, enum pm_device_state expected)
{
	enum pm_device_state state;

	zassert_equal(0, pm_device_state_get(dev, &state));
	zassert_equal(expected, state);
}

static void env_new_data_cb(const struct zbus_channel *chan)
{
	k_sem_give(&env_published);
}

static void task_schedule(struct task_data *data)
{
	data->schedule_idx = 0;
	data->executor.workqueue.reschedule_counter = 0;
	k_poll_signal_init(&data->terminate_signal);
	k_work_reschedule(&data->executor.workqueue.work, K_NO_WAIT);
}

static void expect_logging(uint8_t log_mask, int32_t temperature, uint32_t pressure,
			   uint32_t humidity)
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

	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_AMBIENT_TEMP_PRES_HUM, &tdf);
	if (log_mask & TASK_ENVIRONMENTAL_LOG_TPH) {
		struct tdf_ambient_temp_pres_hum *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(temperature, state->temperature);
		zassert_equal(pressure, state->pressure);
		zassert_equal(humidity / 10, state->humidity);
	} else {
		zassert_equal(-ENOMEM, rc);
	}
	rc = tdf_parse_find_in_buf(pkt->data, pkt->len, TDF_AMBIENT_TEMPERATURE, &tdf);
	if (log_mask & TASK_ENVIRONMENTAL_LOG_T) {
		struct tdf_ambient_temperature *state;

		zassert_equal(0, rc);
		state = tdf.data;

		zassert_equal(temperature, state->temperature);
	} else {
		zassert_equal(-ENOMEM, rc);
	}

	net_buf_unref(pkt);
}

static void test_env(const struct test_configuration *config, uint8_t log_mask)
{
	INFUSE_ZBUS_TYPE(INFUSE_ZBUS_CHAN_AMBIENT_ENV) env_reading;
	struct sensor_value value;
	uint32_t pub_count;

	/* Reset all channel info */
	generic_sim_reset(ENV0, false);
	generic_sim_reset(ENV1, false);

	schedule.task_logging[0].tdf_mask = log_mask;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	if (config->primary.temperature != 0) {
		sensor_value_from_milli(&value, config->primary.temperature);
		generic_sim_channel_set(ENV0, SENSOR_CHAN_AMBIENT_TEMP, value);
	}
	if (config->primary.pressure != 0) {
		sensor_value_from_milli(&value, config->primary.pressure);
		generic_sim_channel_set(ENV0, SENSOR_CHAN_PRESS, value);
	}
	if (config->primary.humidity != 0) {
		sensor_value_from_milli(&value, config->primary.humidity);
		generic_sim_channel_set(ENV0, SENSOR_CHAN_HUMIDITY, value);
	}
	if (config->secondary.temperature != 0) {
		sensor_value_from_milli(&value, config->secondary.temperature);
		generic_sim_channel_set(ENV1, SENSOR_CHAN_AMBIENT_TEMP, value);
	}
	if (config->secondary.pressure != 0) {
		sensor_value_from_milli(&value, config->secondary.pressure);
		generic_sim_channel_set(ENV1, SENSOR_CHAN_PRESS, value);
	}
	if (config->secondary.humidity != 0) {
		sensor_value_from_milli(&value, config->secondary.humidity);
		generic_sim_channel_set(ENV1, SENSOR_CHAN_HUMIDITY, value);
	}

	/* Clear state */
	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	(void)k_sem_take(&env_published, K_NO_WAIT);

	/* Schedule task */
	task_schedule(&data);

	k_sleep(K_MSEC(500));

	/* Task should be complete */
	zassert_equal(0, k_work_delayable_busy_get(&data.executor.workqueue.work));
	zassert_equal(pub_count + 1, zbus_chan_pub_stats_count(ZBUS_CHAN));
	zbus_chan_read(ZBUS_CHAN, &env_reading, K_FOREVER);
	zassert_equal(config->output.temperature, env_reading.temperature);
	zassert_equal(config->output.pressure, env_reading.pressure);
	zassert_equal(config->output.humidity / 10, env_reading.humidity);

	expect_logging(log_mask, config->output.temperature, config->output.pressure,
		       config->output.humidity);
}

static void test_cfg(const struct test_configuration *config)
{
	uint8_t log_all = TASK_ENVIRONMENTAL_LOG_TPH | TASK_ENVIRONMENTAL_LOG_T;

	test_env(config, 0);
	test_env(config, TASK_ENVIRONMENTAL_LOG_T);
	test_env(config, TASK_ENVIRONMENTAL_LOG_TPH);
	test_env(config, log_all);
}

ZTEST(task_env, test_all_errors)
{
	const struct test_configuration c1 = {0};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* No channels configured, every reading should fallback to its error state */
	test_cfg(&c1);
}

ZTEST(task_env, test_temperature_single)
{
	const struct test_configuration c1 = {
		.primary = {.temperature = 27123},
		.output = {.temperature = 27123},
	};
	const struct test_configuration c2 = {
		.secondary = {.temperature = 27123},
	};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* Only temperature, one source */
	test_cfg(&c1);
	test_cfg(&c2);
}

ZTEST(task_env, test_temperature_dual)
{
	const struct test_configuration c1 = {
		.primary = {.temperature = 27123},
		.output = {.temperature = 27123},
	};
	const struct test_configuration c2 = {
		.secondary = {.temperature = 26123},
		.output = {.temperature = 26123},
	};
	const struct test_configuration c3 = {
		.primary = {.temperature = 28123},
		.secondary = {.temperature = 26123},
		.output = {.temperature = 28123},
	};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config_dual, &data, 1);

	/* Only temperature, dual source */
	test_cfg(&c1);
	test_cfg(&c2);
	test_cfg(&c3);
}

ZTEST(task_env, test_all_single)
{
	const struct test_configuration c1 = {
		.primary = {.temperature = 27123, .pressure = 100567, .humidity = 56000},
		.output = {.temperature = 27123, .pressure = 100567, .humidity = 56000},
	};
	const struct test_configuration c2 = {
		.secondary = {.temperature = 27123, .pressure = 100567, .humidity = 43250},
	};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config, &data, 1);

	/* All channels, one source */
	test_cfg(&c1);
	test_cfg(&c2);
}

ZTEST(task_env, test_all_dual)
{
	const struct test_configuration c1 = {
		.secondary = {.temperature = 8542, .pressure = 101764, .humidity = 28337},
		.output = {.temperature = 8542, .pressure = 101764, .humidity = 28337},
	};
	const struct test_configuration c2 = {
		.secondary = {.temperature = -15672, .pressure = 101567, .humidity = 73250},
		.output = {.temperature = -15672, .pressure = 101567, .humidity = 73250},
	};
	const struct test_configuration c3 = {
		.primary = {.temperature = 37173, .pressure = 99754, .humidity = 12000},
		.secondary = {.temperature = 27123, .pressure = 106567, .humidity = 18000},
		.output = {.temperature = 37173, .pressure = 99754, .humidity = 12000},
	};

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config_dual, &data, 1);

	/* All channels, dual source */
	test_cfg(&c1);
	test_cfg(&c2);
	test_cfg(&c3);
}

ZTEST(task_env, test_failures)
{
	const struct test_configuration c1 = {
		.primary = {.temperature = 37173, .pressure = 99754, .humidity = 12000},
		.secondary = {.temperature = 27123, .pressure = 106567, .humidity = 18000},
		.output = {.temperature = 27123, .pressure = 106567, .humidity = 18000},
	};
	const struct test_configuration c2 = {
		.primary = {.temperature = 47333},
		.secondary = {.temperature = 27123, .pressure = 106567, .humidity = 18000},
		.output = {.temperature = 47333},
	};
	const struct test_configuration c3 = {
		.primary = {.pressure = 99758},
		.secondary = {.temperature = 27123, .pressure = 106567, .humidity = 18000},
		.output = {.pressure = 99758},
	};
	uint32_t pub_count;

	schedule = (struct task_schedule){
		.task_id = TASK_ID_ENVIRONMENTAL,
		.validity = TASK_VALID_ALWAYS,
	};

	/* Setup links between task config and data */
	task_runner_init(&schedule, &state, 1, &config_dual, &data, 1);

	/* Primary device fails for any reason, falls back to secondary */
	if (IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		generic_sim_func_rc(ENV0, -EIO, 0, 0);
		test_cfg(&c1);
		expected_pm_state(ENV0, PM_DEVICE_STATE_SUSPENDED);
	}
	generic_sim_func_rc(ENV0, 0, 0, -EIO);
	test_cfg(&c1);
	expected_pm_state(ENV0, PM_DEVICE_STATE_SUSPENDED);
	generic_sim_func_rc(ENV0, 0, 0, 0);

	/* Secondary device fails for any reason */
	if (IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		generic_sim_func_rc(ENV1, -EIO, 0, 0);
		test_cfg(&c2);
		test_cfg(&c3);
		expected_pm_state(ENV1, PM_DEVICE_STATE_SUSPENDED);
	}
	generic_sim_func_rc(ENV1, 0, 0, -EIO);
	test_cfg(&c2);
	test_cfg(&c3);
	expected_pm_state(ENV1, PM_DEVICE_STATE_SUSPENDED);
	generic_sim_func_rc(ENV1, 0, 0, 0);

	/* Both sensors fail, no logging or publishing  */
	generic_sim_func_rc(ENV0, 0, 0, -EIO);
	generic_sim_func_rc(ENV1, 0, 0, -EIO);
	schedule.task_logging[0].tdf_mask = TASK_ENVIRONMENTAL_LOG_TPH | TASK_ENVIRONMENTAL_LOG_T;
	schedule.task_logging[0].loggers = TDF_DATA_LOGGER_SERIAL;

	pub_count = zbus_chan_pub_stats_count(ZBUS_CHAN);
	task_schedule(&data);
	k_sleep(K_MSEC(500));
	zassert_equal(pub_count, zbus_chan_pub_stats_count(ZBUS_CHAN));
	expect_logging(0, 0, 0, 0);
}

static void logger_before(void *fixture)
{
	struct k_fifo *tx_queue = epacket_dummmy_transmit_fifo_get();
	struct net_buf *pkt;

	generic_sim_reset(ENV0, true);
	generic_sim_reset(ENV1, true);
	tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
	pkt = k_fifo_get(tx_queue, K_MSEC(10));
	if (pkt) {
		net_buf_unref(pkt);
	}
	k_sem_reset(&env_published);
}

ZTEST_SUITE(task_env, NULL, NULL, logger_before, NULL, NULL);
