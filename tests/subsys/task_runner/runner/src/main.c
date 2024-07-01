/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/task_runner/runner.h>

enum task_ids {
	TASK_ID_SLEEPY = 113,
	TASK_ID_WORKQ = 239,
};

static k_timeout_t example_task_block_timeout;
static int example_task_expected_block_rc;
static int example_task_run_cnt;

void example_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate)
{
	int rc;

	zassert_not_null(schedule);
	zassert_not_null(terminate);
	example_task_run_cnt += 1;

	/* Block for the expected duration */
	rc = task_runner_task_block(terminate, example_task_block_timeout);
	/* Ensure result matches the expected value */
	zassert_equal(example_task_expected_block_rc, rc);
}

#define SLEEPY_TASK(define_mem, define_config)                                                     \
	IF_ENABLED(define_mem, (K_THREAD_STACK_DEFINE(sleep_stack_area, 2048)))                    \
	IF_ENABLED(define_config,                                                                  \
		   ({                                                                              \
			   .name = "sleepy",                                                       \
			   .task_id = TASK_ID_SLEEPY,                                              \
			   .exec_type = TASK_EXECUTOR_THREAD,                                      \
			   .executor.thread =                                                      \
				   {                                                               \
					   .task_fn = example_task_fn,                             \
					   .stack = sleep_stack_area,                              \
					   .stack_size = K_THREAD_STACK_SIZEOF(sleep_stack_area),  \
				   },                                                              \
		   }))

static k_timeout_t example_workqueue_reschedule_delay;
static int example_workqueue_reschedule_cnt;
static int example_workqueue_run_cnt;

void example_workqueue_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);

	if (task->executor.workqueue.reschedule_counter == 0) {
		/* Increment on first entry only */
		example_workqueue_run_cnt += 1;
	}

	/* Reschedule until limit reached */
	if (task->executor.workqueue.reschedule_counter < example_workqueue_reschedule_cnt) {
		task_workqueue_reschedule(task, example_workqueue_reschedule_delay);
	}
}

#define WORKQUEUE_TASK(define_mem, define_config)                                                  \
	IF_ENABLED(define_config, ({.name = "workq",                                               \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_workqueue_fn,                     \
				    }}))

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, SLEEPY_TASK, WORKQUEUE_TASK);

ZTEST(task_runner_runner, test_init_invalid)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY + 1,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 4,
		},
	};
	struct task_schedule_state states[ARRAY_SIZE(schedules)];
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	/* Schedule refers to task that does not exist */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_MSEC(10));
		iter++;
	}
	zassert_equal(0, example_task_run_cnt);

	/* Schedule is invalid */
	schedules[0].task_id = TASK_ID_SLEEPY;
	schedules[0].battery_start_threshold = 110;
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_MSEC(10));
		iter++;
	}
	zassert_equal(0, example_task_run_cnt);
}

ZTEST(task_runner_runner, test_basic_behaviour)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 4,
		},
	};
	struct task_schedule_state states[ARRAY_SIZE(schedules)];
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Immediate termination (10 seconds with 5 second period == 2 runs) */
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);

	/* "run" for a few seconds before terminating */
	example_task_block_timeout = K_SECONDS(3);
	example_task_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);

	/* Block until runner requests termination */
	example_task_block_timeout = K_FOREVER;
	example_task_expected_block_rc = 1;
	example_task_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
}

ZTEST(task_runner_runner, test_multi_schedule)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 4,
		},
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 111111,
			/* Short timeout, ensure this doesn't happen */
			.timeout_s = 1,
		},
	};
	struct task_schedule_state states[ARRAY_SIZE(schedules)];
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* "run" for a few seconds before terminating.
	 * If second schedule terminates first, we would see a different return code
	 */
	example_task_block_timeout = K_SECONDS(3);
	example_task_expected_block_rc = 0;
	example_task_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
}

ZTEST(task_runner_runner, test_workqueue_task)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
		},
	};
	struct task_schedule_state states[ARRAY_SIZE(schedules)];
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Immediate termination (10 seconds with 5 second period == 2 runs) */
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_workqueue_run_cnt);

	example_workqueue_reschedule_cnt = 10;

	/* "run" for a few seconds before terminating */
	example_workqueue_reschedule_delay = K_MSEC(200);
	example_workqueue_reschedule_cnt = 10;
	example_workqueue_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_MS(iter * MSEC_PER_SEC));
		iter++;
	}
	zassert_equal(2, example_workqueue_run_cnt);
}

static void runner_before(void *fixture)
{
	example_task_block_timeout = K_NO_WAIT;
	example_task_expected_block_rc = 0;
	example_task_run_cnt = 0;
	example_workqueue_run_cnt = 0;
}

ZTEST_SUITE(task_runner_runner, NULL, NULL, runner_before, NULL, NULL);
