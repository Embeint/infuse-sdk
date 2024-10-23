/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/states.h>
#include <infuse/zbus/channels.h>

#include <infuse/task_runner/runner.h>
#include <infuse/task_runner/tasks/infuse_tasks.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_IMU,
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 50,
		.task_args.infuse.imu =
			{
				.accelerometer =
					{
						.range_g = 2,
						.rate_hz = 50,
					},
				.fifo_sample_buffer = 50,
			},
	},
	{
		.task_id = TASK_ID_MOTION_ID,
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 50,
		.task_args.infuse.motion_id =
			{
				.in_motion_timeout = 2,
				.threshold_millig = 100,
			},
	},
};
struct task_schedule_state states[ARRAY_SIZE(schedules)];

#if DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1))
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#endif /* DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1))*/

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (IMU_TASK, DEVICE_DT_GET(DT_ALIAS(imu0))),
			 (MOTION_ID_TASK));

INFUSE_ZBUS_CHAN_DECLARE(INFUSE_ZBUS_CHAN_IMU);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_IMU)

int main(void)
{
	int ret;
	/* Start the watchdog */
	(void)infuse_watchdog_start();

	/* Initialise task runner */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start auto iteration */
	task_runner_start_auto_iterate();

#if DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1))
	/* Initialise LEDs */
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return 0;
	}
#endif /* DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1)) */

	/* Print status and switch LEDs based on movement if available*/
	while (true) {
		if (infuse_state_get(INFUSE_STATE_DEVICE_MOVING)) {
			LOG_INF("Device is moving");
#if DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1))
			gpio_pin_set_dt(&led0, 1);
			gpio_pin_set_dt(&led1, 0);
#endif /* DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1)) */
		} else {
			LOG_INF("Device is stationary");
#if DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1))
			gpio_pin_set_dt(&led0, 0);
			gpio_pin_set_dt(&led1, 1);
#endif /* DT_NODE_EXISTS(DT_ALIAS(led0)) && DT_NODE_EXISTS(DT_ALIAS(led1)) */
		}
		k_sleep(K_MSEC(500));
	}
}
