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

#include <infuse/fs/kv_store.h>
#include <infuse/states.h>
#include <infuse/task_runner/runner.h>

enum task_ids {
	TASK_ID_NO_ARG = 10,
	TASK_ID_SLEEPY = 113,
	TASK_ID_WORKQ = 239,
};

static k_timeout_t example_task_block_timeout;
static int example_task_expected_block_rc;
static uint8_t example_task_expected_arg;
static int example_task_run_cnt;

struct sleepy_args {
	void *some_function_pointer;
	int should_be_two;
};

static void example_task_fn(const struct task_schedule *schedule, struct k_poll_signal *terminate,
			    void *arg)
{
	const struct sleepy_args *args = arg;
	int rc;

	zassert_not_null(schedule);
	zassert_not_null(terminate);
	example_task_run_cnt += 1;

	/* Validate expected schedule argument value */
	zassert_equal(example_task_expected_arg, schedule->task_args.raw[0]);

	/* Validate expected compile-time argument value */
	zassert_equal(example_task_fn, args->some_function_pointer);
	zassert_equal(2, args->should_be_two);

	/* Block for the expected duration */
	rc = task_runner_task_block(terminate, example_task_block_timeout);
	/* Ensure result matches the expected value */
	zassert_equal(example_task_expected_block_rc, rc);
}

#define SLEEPY_TASK(define_mem, define_config, arg1, arg2)                                         \
	IF_ENABLED(define_mem, (K_THREAD_STACK_DEFINE(sleep_stack_area, 2048);                     \
				struct k_thread sleep_thread_obj;                                  \
				const struct sleepy_args sleepy_args_inst = {arg1, arg2}))         \
	IF_ENABLED(define_config,                                                                  \
		   ({                                                                              \
			   .name = "sleepy",                                                       \
			   .task_id = TASK_ID_SLEEPY,                                              \
			   .exec_type = TASK_EXECUTOR_THREAD,                                      \
			   .task_arg.const_arg = &sleepy_args_inst,                                \
			   .executor.thread =                                                      \
				   {                                                               \
					   .thread = &sleep_thread_obj,                            \
					   .task_fn = example_task_fn,                             \
					   .stack = sleep_stack_area,                              \
					   .stack_size = K_THREAD_STACK_SIZEOF(sleep_stack_area),  \
				   },                                                              \
		   }))

static k_timeout_t example_workqueue_reschedule_delay;
static int example_workqueue_reschedule_cnt;
static uint8_t example_workqueue_expected_arg;
static int example_workqueue_run_cnt;

static void example_workqueue_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	const struct task_schedule *sch = task_schedule_from_data(task);

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		return;
	}

	if (task->executor.workqueue.reschedule_counter == 0) {
		/* Increment on first entry only */
		example_workqueue_run_cnt += 1;
	}

	/* Validate expected schedule argument value */
	zassert_equal(example_workqueue_expected_arg, sch->task_args.raw[0]);

	/* Validate expected compile-time argument value */
	zassert_equal(example_task_fn, task->executor.workqueue.task_arg.const_arg);

	/* Reschedule until limit reached */
	if (task->executor.workqueue.reschedule_counter < example_workqueue_reschedule_cnt) {
		task_workqueue_reschedule(task, example_workqueue_reschedule_delay);
	}
}

#define WORKQUEUE_TASK(define_mem, define_config, ptr)                                             \
	IF_ENABLED(define_config, ({.name = "workq",                                               \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .task_arg.arg = ptr,                                           \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_workqueue_fn,                     \
				    }}))

#define NO_ARG_TASK(define_mem, define_config, ...)                                                \
	IF_ENABLED(define_config, ({.name = "no_arg",                                              \
				    .task_id = TASK_ID_NO_ARG,                                     \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_workqueue_fn,                     \
				    }}))

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (SLEEPY_TASK, example_task_fn, 2),
			 (WORKQUEUE_TASK, example_task_fn), (NO_ARG_TASK));

ZTEST(task_runner_runner, test_init_invalid)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY + 1,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 4,
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	/* Schedule refers to task that does not exist */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_MSEC(10));
		iter++;
	}
	zassert_equal(0, example_task_run_cnt);

	/* Schedule is invalid */
	schedules[0].task_id = TASK_ID_SLEEPY;
	schedules[0].battery_start.lower = 110;
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_MSEC(10));
		iter++;
	}
	zassert_equal(0, example_task_run_cnt);
}

ZTEST(task_runner_runner, test_init_duplicate_task_ids)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_NO_ARG,
		},
	};

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};
	TASK_RUNNER_TASKS_DEFINE(dup_tasks, dup_tasks_data, (NO_ARG_TASK), (NO_ARG_TASK));

	/* Warning text should be output */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), dup_tasks, dup_tasks_data,
			 ARRAY_SIZE(dup_tasks));
}

ZTEST(task_runner_runner, test_schedule_linking)
{
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_NO_ARG,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_AFTER,
			.periodicity.after =
				{
					.schedule_idx = 1,
					.duration_s = 10,
				},
		},
		{
			.task_id = TASK_ID_NO_ARG,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_AFTER,
			.periodicity.after =
				{
					.schedule_idx = 2,
					.duration_s = 10,
				},
		},
	};

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};
	TASK_RUNNER_TASKS_DEFINE(oob, oob_data, (NO_ARG_TASK));

	/* Warning text should be output */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), oob, oob_data, ARRAY_SIZE(oob));

	zassert_not_null(states[0].linked);
	zassert_is_null(states[1].linked);
}

DEVICE_DEFINE(dummy_device, "dummy", NULL, NULL, NULL, NULL, POST_KERNEL, 0, NULL);
static int example_device_run;

static void example_device_fn(struct k_work *work)
{
	example_device_run++;
}

#define DEVICE_TASK(define_mem, define_config, dev_ptr)                                            \
	IF_ENABLED(define_config, ({.name = "dev",                                                 \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .flags = TASK_FLAG_ARG_IS_DEVICE,                              \
				    .task_arg.dev = dev_ptr,                                       \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = example_device_fn,                        \
				    }}))

ZTEST(task_runner_runner, test_device_not_ready)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
		},
	};
	const struct device *dev = &DEVICE_NAME_GET(dummy_device);

	zassert_equal(0, example_device_run);

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};
	TASK_RUNNER_TASKS_DEFINE(is_ready, is_ready_data,
				 (DEVICE_TASK, &DEVICE_NAME_GET(dummy_device)));

	/* Should run without problems */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), is_ready, is_ready_data,
			 ARRAY_SIZE(is_ready));
	task_runner_iterate(app_states, 20, 20, 100);
	k_sleep(K_MSEC(10));
	zassert_equal(1, example_device_run);

	/* Set initialisation result to failed */
	dev->state->init_res = -ENODEV;

	/* Warning text should be output, should not run */
	task_runner_init(schedules, states, ARRAY_SIZE(schedules), is_ready, is_ready_data,
			 ARRAY_SIZE(is_ready));

	task_runner_iterate(app_states, 21, 21, 100);
	k_sleep(K_MSEC(10));
	zassert_equal(1, example_device_run);
}

static enum task_schedule_event events_recv[8];
static const struct task_schedule *expected_schedule;
static enum task_schedule_event expected_event;
static int callback_count;

static void basic_schedule_callback(const struct task_schedule *schedule,
				    enum task_schedule_event event)
{
	zassert_true(callback_count < ARRAY_SIZE(events_recv));
#ifndef CONFIG_KV_STORE_KEY_TASK_SCHEDULES
	/* Schedules are copied when KV store is enabled */
	zassert_equal(expected_schedule, schedule);
#endif
	events_recv[callback_count++] = event;
}

ZTEST(task_runner_runner, test_basic_behaviour)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 3,
			.task_args.raw = {0xA5},
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	example_task_expected_arg = schedules[0].task_args.raw[0];
	expected_schedule = &schedules[0];
	expected_event = TASK_SCHEDULE_STARTED;

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));
	states[0].event_cb = basic_schedule_callback;

	/* Immediate termination (10 seconds with 5 second period == 2 runs) */
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
	zassert_equal(4, callback_count);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[0]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[1]);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[2]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[3]);

	/* "run" for a few seconds before terminating */
	example_task_block_timeout = K_SECONDS(2);
	example_task_run_cnt = 0;
	callback_count = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
	zassert_equal(4, callback_count);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[0]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[1]);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[2]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[3]);

	/* Block until runner requests termination */
	example_task_block_timeout = K_FOREVER;
	example_task_expected_block_rc = 1;
	example_task_run_cnt = 0;
	callback_count = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
	zassert_equal(6, callback_count);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[0]);
	zassert_equal(TASK_SCHEDULE_TERMINATE_REQUEST, events_recv[1]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[2]);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[3]);
	zassert_equal(TASK_SCHEDULE_TERMINATE_REQUEST, events_recv[4]);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[5]);
}

ZTEST(task_runner_runner, test_after)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_AFTER,
			.periodicity.after.schedule_idx = 1,
			.periodicity.after.duration_s = 2,
			.task_args.raw = {0xA5},
		},
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 10,
			.task_args.raw = {0xA5},
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	example_task_expected_arg = schedules[0].task_args.raw[0];
	example_task_block_timeout = K_MSEC(1800);

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Start on a clean second boundary */
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;

	/* Starts at T = 0, terminates at T = 1.8  */
	for (int i = 0; i < 2; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		zassert_equal(1, example_task_run_cnt);
		iter++;
	}

	/* T= 2 & 3, no running */
	for (int i = 0; i < 2; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		zassert_equal(1, example_task_run_cnt);
		iter++;
	}

	/* Starts again at T = 4 (2 seconds after termination) */
	for (int i = 0; i < 2; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		zassert_equal(2, example_task_run_cnt);
		iter++;
	}
}

ZTEST(task_runner_runner, test_permanent)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_PERMANENTLY_RUNS,
			/* Scheduling arguments will be ignored */
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.timeout_s = 4,
			.task_args.raw = {0xA5},
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	example_task_block_timeout = K_FOREVER;
	example_task_expected_block_rc = 1;
	example_task_expected_arg = schedules[0].task_args.raw[0];

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Scheduling arguments ignore, always running */
	for (int i = 0; i < 30; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(1, example_task_run_cnt);

	/* Manually kill the task */
	k_poll_signal_raise(&app_tasks_data[0].terminate_signal, 0);
	k_sleep(K_MSEC(10));

	/* Should be immediately restarted */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_MSEC(10));
	zassert_equal(2, example_task_run_cnt);

	/* Terminate again to cleanup the test */
	k_poll_signal_raise(&app_tasks_data[0].terminate_signal, 0);
	k_sleep(K_MSEC(10));
}

ZTEST(task_runner_runner, test_multi_schedule)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
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
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* "run" for a few seconds before terminating.
	 * If second schedule terminates first, we would see a different return code
	 */
	example_task_block_timeout = K_SECONDS(3);
	example_task_expected_block_rc = 0;
	example_task_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);
}

ZTEST(task_runner_runner, test_workqueue_task)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.timeout_s = 4,
			.periodicity_type = TASK_PERIODICITY_FIXED,
			.periodicity.fixed.period_s = 5,
			.task_args.raw = {0xB2},
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};

	example_workqueue_expected_arg = schedules[0].task_args.raw[0];

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), app_tasks, app_tasks_data,
			 ARRAY_SIZE(app_tasks));

	/* Immediate termination (10 seconds with 5 second period == 2 runs) */
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_workqueue_run_cnt);

	example_workqueue_reschedule_cnt = 10;

	/* "run" for a few seconds before terminating */
	example_workqueue_reschedule_delay = K_MSEC(200);
	example_workqueue_reschedule_cnt = 10;
	example_workqueue_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_workqueue_run_cnt);

	/* Run until runner requests termination */
	example_workqueue_reschedule_delay = K_SECONDS(10);
	example_workqueue_reschedule_cnt = INT_MAX;
	example_workqueue_run_cnt = 0;
	for (int i = 0; i < 10; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_workqueue_run_cnt);
}

static void long_block_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		return;
	}

	/* Do some long work */
	k_sleep(K_SECONDS(2));

	/* Attempt to run again in 5 seconds */
	task_workqueue_reschedule(task, K_SECONDS(5));
}

#define LONG_BLOCK_TASK(define_mem, define_config, ...)                                            \
	IF_ENABLED(define_config, ({.name = "long_block",                                          \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = long_block_fn,                            \
				    }}))

ZTEST(task_runner_runner, test_workqueue_reschedule_override)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.timeout_s = 1,
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};
	TASK_RUNNER_TASKS_DEFINE(long_block, long_block_data, (LONG_BLOCK_TASK));

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), long_block, long_block_data,
			 ARRAY_SIZE(long_block));

	/* Iterate runner to boot the task */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_SECONDS(1));
	/* Iterate again, which should trigger the timeout after the task has checked the signal but
	 * before it runs task_workqueue_reschedule.
	 */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	/* Sleep should have expired */
	k_sleep(K_SECONDS(2));

	/* Validate that work is idle.
	 * This will only pass if `task_workqueue_reschedule` overrides the requested delay.
	 */
	zassert_equal(0, k_work_delayable_busy_get(&long_block_data->executor.workqueue.work));
}

static void workqueue_persistent(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);
	uint8_t *persistent = task_schedule_persistent_storage(task);

	/* Increment persistent storage */
	persistent[0] += 1;
}

#define WORKQUEUE_PERSISTENT_TASK(define_mem, define_config, ...)                                  \
	IF_ENABLED(define_config, ({.name = "workq",                                               \
				    .task_id = TASK_ID_WORKQ,                                      \
				    .exec_type = TASK_EXECUTOR_WORKQUEUE,                          \
				    .executor.workqueue = {                                        \
					    .worker_fn = workqueue_persistent,                     \
				    }}))

ZTEST(task_runner_runner, test_workqueue_persistent_mem)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_LOCKOUT,
			.periodicity.lockout.lockout_s = 5,
		},
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_LOCKOUT,
			.periodicity.lockout.lockout_s = 3,
		},
	};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;

	TASK_SCHEDULE_STATES_DEFINE(states, schedules) = {0};
	TASK_RUNNER_TASKS_DEFINE(peristent_mem, peristent_mem_data, (WORKQUEUE_PERSISTENT_TASK));

	task_runner_init(schedules, states, ARRAY_SIZE(schedules), peristent_mem,
			 peristent_mem_data, ARRAY_SIZE(peristent_mem));

	/* Loop 30 times */
	for (int i = 0; i < 30; i++) {
		/* Iterate runner to boot the task */
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_SECONDS(1));
	}

	/* Persistent memory should be different per schedule state */
	zassert_equal(29 / 5, states[0].runtime_state[0]);
	zassert_equal(29 / 3, states[1].runtime_state[0]);
}

static void runner_before(void *fixture)
{
	example_task_block_timeout = K_NO_WAIT;
	example_task_expected_block_rc = 0;
	example_task_expected_arg = 0;
	example_task_run_cnt = 0;
	example_workqueue_run_cnt = 0;
#ifdef CONFIG_KV_STORE
	kv_store_reset();
#endif
}

ZTEST_SUITE(task_runner_runner, NULL, NULL, runner_before, NULL, NULL);
