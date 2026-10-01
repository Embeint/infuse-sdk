/**
 * @file
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/kernel.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/buf_loader.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/net/conn_mgr_connectivity.h>

#include <infuse/data_logger/logger.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/time/epoch.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/fs/littlefs.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>
#include <infuse/work_q.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

#include <infuse/algorithm_runner/runner.h>

#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
static const uint8_t gesture_algorithm[] __aligned(sizeof(void *)) = {
#include "gesture_algorithm.inc"
};
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_IMU,
		.validity = TASK_VALID_ALWAYS,
		.task_logging =
			{
				{
					.loggers = TDF_DATA_LOGGER_SERIAL |
						   TDF_DATA_LOGGER_BT_PERIPHERAL,
					.tdf_mask = TASK_IMU_LOG_ACC | TASK_IMU_LOG_GYR,
				},
			},
		.task_args.imu =
			{
				.accelerometer =
					{
						.range_g = 2,
						.rate_hz = 30,
					},
				.gyroscope =
					{
						.range_dps = 500,
						.rate_hz = 15,
					},
				.fifo_sample_buffer = 50,
			},
	},
#ifdef CONFIG_BT
	{
		.task_id = TASK_ID_TDF_LOGGER,
		.validity = TASK_VALID_ALWAYS,
		.task_args.tdf_logger =
			{
				.loggers = TDF_DATA_LOGGER_BT_ADV,
				.logging_period_ms = 900,
				.random_delay_ms = 250,
				.tdfs = TASK_TDF_LOGGER_LOG_ANNOUNCE | TASK_TDF_LOGGER_LOG_ACCEL,
			},
	},
#endif /* CONFIG_BT */
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (TDF_LOGGER_TASK, NULL),
			 (IMU_TASK, DEVICE_DT_GET(DT_ALIAS(imu0))));

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});

struct led_handler {
	struct k_work_delayable work;
	const struct gpio_dt_spec *led;
};

static struct led_handler led2_runner;

/* Pulse the specified LED for a short duration. */
static void led_pulse_dt(const struct gpio_dt_spec *led, k_timeout_t duration)
{
	if (led->port) {
		gpio_pin_set_dt(led, 1);
		k_sleep(duration);
		gpio_pin_set_dt(led, 0);
	}
}

/**
 * @brief Queue a pulse for the specified LED using a work item (background task).
 */
static void queue_led_pulse(struct k_work_delayable *work, k_timeout_t duration)
{
	struct led_handler *handler = CONTAINER_OF(work, struct led_handler, work);

	if (handler->led->port) {
		gpio_pin_set_dt(handler->led, 1);
		infuse_work_schedule(work, duration);
	}
}

/**
 * @brief Handle clearing the LED after a pulse as a background work item.
 *
 */
static void handle_led_clear(struct k_work *work)
{
	struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
	struct led_handler *handler = CONTAINER_OF(delayable_work, struct led_handler, work);
	gpio_pin_set_dt(handler->led, 0);
}

/**
 * @brief data logger flushed callback to trigger LED pulse.
 *
 */
static void bt_log_flushed_cb(const struct device *dev, enum infuse_type data_type, void *user_data)
{
	/* Blink the Blue LED to indicate that the BT log has been flushed */
	queue_led_pulse(&led2_runner.work, K_MSEC(2));
}

struct data_logger_cb callbacks = {
	.write_success = bt_log_flushed_cb,
};

/**
 * @brief Expose a method to broadcast inference results data via the data logger.
 *
 * @param length sizeof(data)
 * @param start_ticks time of the first event in system ticks
 * @param data TDF_ALGORITHM_CLASS_TIME_SERIES data.
 */
void broadcast_inference(uint8_t length, uint64_t start_ticks, const void *data)
{
	uint64_t epoch_time = epoch_time_from_ticks(start_ticks);
	tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
			    TDF_ALGORITHM_CLASS_TIME_SERIES, length, epoch_time, data);
}

/**
 * @brief Expose a method to pulse `led0` during inference.
 *
 */
void inference_led_pulse(bool start)
{
	if (led0.port) {
		gpio_pin_set_dt(&led0, start ? 1 : 0);
	}
}

/* Export symbols to llext algorithms */
EXPORT_GROUP_SYMBOL(INFUSE_ALG, inference_led_pulse);
EXPORT_GROUP_SYMBOL(INFUSE_ALG, broadcast_inference);

int main(void)
{
	const struct device *logger = DEVICE_DT_GET(DT_NODELABEL(data_logger_bt_peripheral));
	const struct infuse_algorithm *algorithm = NULL;
	int rc;

	/* Start the watchdog */
	(void)infuse_watchdog_start();

#ifdef CONFIG_INFUSE_LITTLEFS
	struct infuse_littlefs_fs_info fs_info;

	/* Common boot may have already mounted the filesystem. */
	rc = infuse_littlefs_fs_info(&fs_info);
	if (rc == -EAGAIN) {
		rc = infuse_littlefs_init();
	}
	if (rc < 0) {
		LOG_ERR("Failed to initialise LittleFS (%d)", rc);
		return rc;
	}
#endif /* CONFIG_INFUSE_LITTLEFS */

#ifdef CONFIG_NETWORKING
	conn_mgr_all_if_up(true);
	conn_mgr_all_if_connect(true);
#endif /* CONFIG_NETWORKING */

	/* Initialise LEDs */
	if (led0.port) {
		gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	}
	if (led1.port) {
		gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	}
	if (led2.port) {
		gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
	}

	/* Initialise work items for LEDs */
	led2_runner.led = &led2;
	k_work_init_delayable(&led2_runner.work, handle_led_clear);

	/* Register LED blink callback on the bt_peripheral data logger */
	data_logger_register_cb(logger, &callbacks);

	/* Load the dynamic algorithm */
#ifdef CONFIG_TEST_ALGORITHM_BUILD_LLEXT
	struct llext_buf_loader buf_loader =
		LLEXT_BUF_LOADER(gesture_algorithm, sizeof(gesture_algorithm));
	struct llext_loader *loader = &buf_loader.loader;
	struct llext_load_param ldr_parm = LLEXT_LOAD_PARAM_DEFAULT;
	struct llext *ext;

	/* Load the ELF file */
	rc = llext_load(loader, "gesture_alg", &ext, &ldr_parm);
	if (rc < 0) {
		LOG_ERR("Failed to load gesture algorithm (%d)", rc);
		return rc;
	}

	/* Find the algorithm struct that we expect to be exported */
	algorithm = llext_find_sym(&ext->exp_tab, "algorithm_config");
	if (algorithm == NULL) {
		LOG_ERR("Failed to find algorithm symbols");
		return -EINVAL;
	}
#endif /* CONFIG_TEST_ALGORITHM_BUILD_LLEXT */

#ifdef CONFIG_TEST_ALGORITHM_BUILD_NATIVE
	extern const struct infuse_algorithm gesture_algorithm;

	algorithm = &gesture_algorithm;
#endif /* CONFIG_TEST_ALGORITHM_BUILD_NATIVE */

	/* Start the algorithm runner */
	algorithm_runner_init();
	if (algorithm != NULL) {
		rc = algorithm_runner_register(algorithm);
		if (rc < 0) {
			LOG_ERR("Failed to register algorithm");
		}
	}

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

	/* Blink the LED1 every 10s to indicate the device is running */
	while (1) {
		led_pulse_dt(&led1, K_MSEC(5));
		k_sleep(K_SECONDS(10));
	}
}
