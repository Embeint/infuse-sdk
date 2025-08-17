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

#include <infuse/states.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/task_runner/runner.h>

enum task_ids {
	TASK_ID_WORKQ = 100,
};

void example_workqueue_fn(struct k_work *work)
{
	(void)work;
}

#define WORKQUEUE_TASK(define_mem, define_config, ...)                                             \
	IF_ENABLED(define_config, ({.name = "workq",                                               \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_workqueue_fn,                     \
				    }}))

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (WORKQUEUE_TASK));
K_SEM_DEFINE(watchdog_expired, 0, 1);

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	k_sem_give(&watchdog_expired);
}

ZTEST(task_runner_watchdog, test_watchdog)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 10,
		},
	};
	uint8_t tr_wdog_channel;
	int rc;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules);

	/* Start the watchdog */
	zassert_equal(0, infuse_watchdog_start());

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Run a few times */
	for (int i = 0; i < 5; i++) {
		task_runner_iterate(app_states, k_uptime_seconds(), i, 100);
		k_sleep(K_SECONDS(1));
	}
	/* Manually feed the channel a few times */
	tr_wdog_channel = task_runner_watchdog_channel();
	for (int i = 0; i < 3; i++) {
		infuse_watchdog_feed(tr_wdog_channel);
		k_sleep(K_SECONDS(1));
	}
	/* Ensure watchdog has not expired */
	rc = k_sem_take(&watchdog_expired, K_NO_WAIT);
	zassert_equal(-EBUSY, rc, "Watchdog expired prematurely");

	/* Failing to call task_runner_iterate should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_expired, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS + 100));
	zassert_equal(0, rc, "Watchdog did not expire");
}

void watchdog_teardown(void *fixture)
{
	/* Disable the watchdog to ensure we aren't rebooted */
	wdt_disable(INFUSE_WATCHDOG_DEV);
}

ZTEST_SUITE(task_runner_watchdog, NULL, NULL, NULL, NULL, watchdog_teardown);
