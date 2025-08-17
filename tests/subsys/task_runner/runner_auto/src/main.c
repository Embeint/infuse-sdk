/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

#include <infuse/fs/kv_store.h>
#include <infuse/states.h>
#include <infuse/task_runner/runner.h>
#include <infuse/zbus/channels.h>

enum task_ids {
	TASK_ID_WORKQ = 1,
};

static int example_task_run_cnt;

static void example_workqueue_fn(struct k_work *work)
{
	zassert_not_null(work);
	example_task_run_cnt += 1;
}

#define WORKQUEUE_TASK(define_mem, define_config, ...)                                             \
	IF_ENABLED(define_config, ({.name = "workq",                                               \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_workqueue_fn,                     \
				    }}))

const struct task_schedule schedules[] = {
	{
		.task_id = TASK_ID_WORKQ,
		.validity = TASK_VALID_ALWAYS,
		.battery_start.lower = 50,
	},
};

TASK_SCHEDULE_STATES_DEFINE(states, schedules);
TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (WORKQUEUE_TASK));

INFUSE_ZBUS_CHAN_DEFINE(INFUSE_ZBUS_CHAN_BATTERY);
#define ZBUS_CHAN INFUSE_ZBUS_CHAN_GET(INFUSE_ZBUS_CHAN_BATTERY)

ZTEST(task_runner_auto, test_auto_iterate)
{
	struct tdf_battery_state battery;
	struct k_work_delayable *dwork;

	/* Initialise the schedules */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start the auto iterator */
	dwork = task_runner_start_auto_iterate();

	/* The default battery charge is 0%, so should not be scheduled */
	k_sleep(K_MINUTES(1));
	zassert_equal(0, example_task_run_cnt);

	/* Publish a valid battery */
	battery.voltage_mv = 3700;
	battery.soc = 100;
	zbus_chan_pub(ZBUS_CHAN, &battery, K_FOREVER);

	/* Set CONFIG_TEST_DURATION_HOURS=1440 to test the uint32_t millisecond rollover.
	 * Not enabled as a testcase because it takes ~5 minutes to run under twister and
	 * fails the final test. Runs fine natively.
	 */
	for (int hour = 0; hour < CONFIG_TEST_DURATION_HOURS; hour++) {
		printk("T: %2d.%2d\n", hour / 24, hour % 24);
		/* Wait for next hour (plus the 1 minute delay from above) */
		k_sleep(K_TIMEOUT_ABS_SEC((hour + 1) * SEC_PER_HOUR + SEC_PER_MIN));
	}

	/* Terminate the work to cleanup the test */
	k_work_cancel_delayable(dwork);

	/* Run count should be approximately equal to the uptime */
	printk("Runs: %d/%d\n", example_task_run_cnt, CONFIG_TEST_DURATION_HOURS * SEC_PER_HOUR);
	zassert_within(CONFIG_TEST_DURATION_HOURS * SEC_PER_HOUR, example_task_run_cnt, 5);
}

ZTEST_SUITE(task_runner_auto, NULL, NULL, NULL, NULL, NULL);
