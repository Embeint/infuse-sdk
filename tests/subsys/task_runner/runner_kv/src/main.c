/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
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

static void example_workqueue_fn(struct k_work *work)
{
	struct task_data *task = task_data_from_work(work);

	if (task_runner_task_block(&task->terminate_signal, K_NO_WAIT) == 1) {
		/* Early wake by runner to terminate */
		return;
	}

	if (task->executor.workqueue.reschedule_counter == 0) {
		/* Reschedule on first entry only */
		task_workqueue_reschedule(task, K_SECONDS(2));
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

TASK_RUNNER_TASKS_DEFINE(app_tasks, app_tasks_data, (SLEEPY_TASK, example_task_fn, 2),
			 (WORKQUEUE_TASK, example_task_fn));

static enum task_schedule_event events_recv[8];
static enum task_schedule_event expected_event;
static int callback_count;

static void basic_schedule_callback(const struct task_schedule *schedule,
				    enum task_schedule_event event)
{
	zassert_true(callback_count < ARRAY_SIZE(events_recv));
	events_recv[callback_count++] = event;
}

ZTEST(task_runner_runner_kv, test_basic_behaviour)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	const struct task_schedule default_schedules[] = {
		{
			.task_id = TASK_ID_WORKQ,
			.validity = TASK_VALID_ALWAYS,
			.periodicity_type = TASK_PERIODICITY_LOCKOUT,
			.periodicity.lockout.lockout_s = 5,
		},
		{
			.task_id = TASK_ID_SLEEPY,
			.validity = TASK_VALID_ALWAYS,
			.timeout_s = 60,
			.task_args.raw = {0xAA},
		},
	};
	struct task_schedule_state states[ARRAY_SIZE(default_schedules)] = {0};
	uint32_t gps_time = 7000;
	uint32_t uptime = 0;
	uint32_t iter = k_uptime_seconds() + 1;
	int rc;

	example_task_expected_block_rc = 1;
	example_task_block_timeout = K_FOREVER;
	example_task_expected_arg = default_schedules[1].task_args.raw[0];
	expected_event = TASK_SCHEDULE_STARTED;

	/* Initialise schedules */
	task_runner_init(default_schedules, states, ARRAY_SIZE(default_schedules), app_tasks,
			 app_tasks_data, ARRAY_SIZE(app_tasks));
	states[1].event_cb = basic_schedule_callback;

	/* Task should have started and still be running (60 second block period) */
	for (int i = 0; i < 30; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(1, example_task_run_cnt);
	zassert_equal(1, callback_count);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[0]);

	/* Update the schedule in the KV store slot with a smaller timeout */
	struct task_schedule updated;

	memcpy(&updated, &default_schedules[1], sizeof(struct task_schedule));
	updated.timeout_s = 5;
	callback_count = 0;

	rc = kv_store_write(KV_KEY_TASK_SCHEDULES + 1, &updated, sizeof(struct task_schedule));
	zassert_equal(sizeof(struct task_schedule), rc);

	/* Next iteration should send out a terminate request */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;
	zassert_equal(1, callback_count);
	zassert_equal(TASK_SCHEDULE_TERMINATE_REQUEST, events_recv[0]);

	/* Iteration after that should see the terminated task and restart it with new args */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;

	zassert_equal(2, example_task_run_cnt);
	zassert_equal(3, callback_count);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[1]);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[2]);
	callback_count = 0;

	/* Task should now timeout after 5 seconds */
	for (int i = 0; i < 5; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		iter++;
	}
	zassert_equal(2, example_task_run_cnt);

	/* Boots again on next run */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;
	zassert_equal(3, example_task_run_cnt);

	/* Changing the default ID should result in a reset to default schedules.
	 * There are no individual schedule update events for a global reset.
	 */
	struct kv_task_schedules_default_id default_id = {132456};

	callback_count = 0;
	example_task_run_cnt = 0;
	rc = kv_store_write(KV_KEY_TASK_SCHEDULES_DEFAULT_ID, &default_id,
			    sizeof(struct kv_task_schedules_default_id));
	zassert_equal(sizeof(struct kv_task_schedules_default_id), rc);

	/* Next iteration sends out the terminations */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;
	zassert_equal(1, callback_count);
	zassert_equal(TASK_SCHEDULE_TERMINATE_REQUEST, events_recv[0]);

	/* Next iteration reloads schedules and restarts */
	task_runner_iterate(app_states, uptime++, gps_time++, 100);
	k_sleep(K_TIMEOUT_ABS_SEC(iter));
	iter++;

	zassert_equal(3, callback_count);
	zassert_equal(TASK_SCHEDULE_STOPPED, events_recv[1]);
	zassert_equal(TASK_SCHEDULE_STARTED, events_recv[2]);
	zassert_equal(1, example_task_run_cnt);

	for (int i = 0; i < 30; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		zassert_equal(1, example_task_run_cnt);
		iter++;
	}

	/* Arbitrary key doesn't trigger anything */
	struct kv_reboots reboots = {100};

	rc = kv_store_write(KV_KEY_REBOOTS, &reboots, sizeof(struct kv_reboots));
	zassert_equal(sizeof(struct kv_reboots), rc);

	for (int i = 0; i < 20; i++) {
		task_runner_iterate(app_states, uptime++, gps_time++, 100);
		k_sleep(K_TIMEOUT_ABS_SEC(iter));
		zassert_equal(1, example_task_run_cnt);
		iter++;
	}
}

static void runner_before(void *fixture)
{
	example_task_block_timeout = K_NO_WAIT;
	example_task_expected_block_rc = 0;
	example_task_expected_arg = 0;
	example_task_run_cnt = 0;
	kv_store_reset();
}

ZTEST_SUITE(task_runner_runner_kv, NULL, NULL, runner_before, NULL, NULL);
