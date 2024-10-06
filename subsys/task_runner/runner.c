/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include <infuse/drivers/watchdog.h>
#include <infuse/task_runner/runner.h>

static const struct task_schedule *sch;
static struct task_schedule_state *sch_states;
static uint8_t sch_num;
static const struct task_config *tsk;
static struct task_data *tsk_states;
static uint8_t tsk_num;

struct k_work_q task_runner_workq;

K_THREAD_STACK_DEFINE(workq_stack_area, CONFIG_TASK_RUNNER_WORKQ_STACK_SIZE);

LOG_MODULE_REGISTER(task_runner, CONFIG_TASK_RUNNER_LOG_LEVEL);

struct k_work_q *task_runner_work_q(void)
{
	return &task_runner_workq;
}

static int task_runner_workqueue_init(void)
{
	/* Boot the task runner workqueue */
	k_work_queue_init(&task_runner_workq);
	k_work_queue_start(&task_runner_workq, workq_stack_area,
			   K_THREAD_STACK_SIZEOF(workq_stack_area),
			   CONFIG_SYSTEM_WORKQUEUE_PRIORITY, NULL);
	k_thread_name_set(k_work_queue_thread_get(&task_runner_workq), "task_workq");
	return 0;
}

SYS_INIT(task_runner_workqueue_init, POST_KERNEL, 0);

void task_runner_init(const struct task_schedule *schedules,
		      struct task_schedule_state *schedule_states, uint8_t num_schedules,
		      const struct task_config *tasks, struct task_data *task_states,
		      uint8_t num_tasks)
{
	sch = schedules;
	sch_states = schedule_states;
	sch_num = num_schedules;
	tsk = tasks;
	tsk_states = task_states;
	tsk_num = num_tasks;

	for (int i = 0; i < tsk_num; i++) {
		tsk_states[i].running = false;
		tsk_states[i].schedule_idx = UINT8_MAX;
		/* Validate that device initialised properly */
		if (tsk[i].flags & TASK_FLAG_ARG_IS_DEVICE) {
			if (!device_is_ready(tsk[i].task_arg.dev)) {
				LOG_WRN("Task %d device '%s' failed to initialise", i,
					tsk[i].task_arg.dev->name);
				tsk_states[i].skip = true;
			}
		}
		/* Initialise delayable workers */
		if (tsk[i].exec_type == TASK_EXECUTOR_WORKQUEUE) {
			tsk_states[i].executor.workqueue.task_arg.const_arg =
				tsk[i].task_arg.const_arg;
			k_work_init_delayable(&tsk_states[i].executor.workqueue.work,
					      tsk[i].executor.workqueue.worker_fn);
		}
		/* Check for duplicate task definitions */
		for (int j = 0; j < i; j++) {
			if (tsk[i].task_id == tsk[j].task_id) {
				LOG_WRN("Task %d and %d share a task ID (%d)!", j, i,
					tsk[i].task_id);
			}
		}
	}

	for (int i = 0; i < sch_num; i++) {
		/* Mark schedule as invalid */
		sch_states[i].task_idx = UINT8_MAX;
		/* Schedule is valid */
		if (!task_schedule_validate(&sch[i])) {
			LOG_WRN("Schedule %d (Task ID %d) is invalid!", i, sch[i].task_id);
			continue;
		}
		/* Schedule refers to task that exists */
		for (int j = 0; j < tsk_num; j++) {
			if (sch[i].task_id == tsk[j].task_id) {
				sch_states[i].task_idx = j;
				break;
			}
		}
		if (sch_states[i].task_idx == UINT8_MAX) {
			LOG_WRN("Schedule %d refers to Task ID %d which does not exist", i,
				sch[i].task_id);
			continue;
		}
		/* Clear scheduling state */
		sch_states[i].last_run = 0;
		sch_states[i].runtime = 0;
	}
}

const struct task_schedule *task_schedule_from_data(struct task_data *data)
{
	return &sch[data->schedule_idx];
}

void task_workqueue_reschedule(struct task_data *task, k_timeout_t delay)
{
	/* Increment reschedule count */
	task->executor.workqueue.reschedule_counter += 1;
	/* Reschedule on queue */
	k_work_reschedule_for_queue(&task_runner_workq, &task->executor.workqueue.work, delay);
}

static void task_start(uint8_t schedule_index, uint32_t uptime)
{
	const struct task_schedule *s = &sch[schedule_index];
	struct task_schedule_state *state = &sch_states[schedule_index];
	const struct task_config *c = &tsk[state->task_idx];
	struct task_data *d = &tsk_states[state->task_idx];
	k_tid_t tid;

	LOG_DBG("Booting task %s from schedule %d", c->name, schedule_index);

	/* Initialise state information */
	state->last_run = uptime;
	state->runtime = 0;
	d->running = true;
	d->schedule_idx = schedule_index;

	k_poll_signal_init(&d->terminate_signal);

	if (c->exec_type == TASK_EXECUTOR_THREAD) {
		/* Boot the thread */
		tid = k_thread_create(&d->executor.thread, c->executor.thread.stack,
				      c->executor.thread.stack_size,
				      (k_thread_entry_t)c->executor.thread.task_fn, (void *)s,
				      &d->terminate_signal, c->task_arg.arg, 5, 0, K_NO_WAIT);
		/* Set the thread name */
		k_thread_name_set(tid, c->name);
	} else {
		__ASSERT_NO_MSG(c->exec_type == TASK_EXECUTOR_WORKQUEUE);
		/* Reset the reschedule counter */
		d->executor.workqueue.reschedule_counter = 0;
		/* Schedule the work on our work queue */
		k_work_schedule_for_queue(&task_runner_workq, &d->executor.workqueue.work,
					  K_NO_WAIT);
	}
}

static void task_terminate(uint8_t schedule_index)
{
	struct task_schedule_state *state = &sch_states[schedule_index];
	const struct task_config *c = &tsk[state->task_idx];
	struct task_data *d = &tsk_states[state->task_idx];

	LOG_DBG("Requesting task %s to terminate", c->name);
	/* Raise the termination signal */
	k_poll_signal_raise(&d->terminate_signal, 0);
	if (c->exec_type == TASK_EXECUTOR_WORKQUEUE) {
		/* Reschedule immediately to terminate */
		k_work_reschedule_for_queue(&task_runner_workq, &d->executor.workqueue.work,
					    K_NO_WAIT);
	}
}

static bool task_has_terminated(uint8_t task_idx)
{
	const struct task_config *c = &tsk[task_idx];
	struct task_data *d = &tsk_states[task_idx];

	if (c->exec_type == TASK_EXECUTOR_THREAD) {
		if (k_thread_join(&d->executor.thread, K_NO_WAIT) == 0) {
			return true;
		}
	} else {
		__ASSERT_NO_MSG(c->exec_type == TASK_EXECUTOR_WORKQUEUE);
		if (k_work_busy_get(&d->executor.workqueue.work.work) == 0) {
			return true;
		}
	}
	return false;
}

INFUSE_WATCHDOG_REGISTER_SYS_INIT(tr_wdog, CONFIG_TASK_RUNNER_INFUSE_WATCHDOG, wdog_channel,
				  loop_period);

void task_runner_iterate(atomic_t *app_states, uint32_t uptime, uint32_t gps_time,
			 uint8_t battery_charge)
{
	bool transition;

	infuse_watchdog_thread_register(wdog_channel, _current);
	(void)loop_period;

	/* Determine if any running tasks have terminated */
	for (int i = 0; i < tsk_num; i++) {
		if (tsk_states[i].running) {
			if (task_has_terminated(i)) {
				LOG_DBG("Task %s terminated", tsk[i].name);
				tsk_states[i].running = false;
			}
		}
	}

	/* Loop over all schedules */
	for (int i = 0; i < sch_num; i++) {
		const struct task_schedule *s = &sch[i];
		struct task_schedule_state *state = &sch_states[i];

		/* Is the schedule invalid? */
		if (state->task_idx == UINT8_MAX) {
			continue;
		}
		struct task_data *d = &tsk_states[state->task_idx];

		/* Should schedule be skipped? */
		if (d->skip) {
			continue;
		}

		/* Is referred task running due to a different schedule */
		if (d->running && (d->schedule_idx != i)) {
			LOG_DBG("Not evaluating %d as started from %d", i, d->schedule_idx);
			continue;
		}

		/* Should any should change state */
		if (d->running) {
			sch_states[i].runtime += 1;
			transition = task_schedule_should_terminate(s, state, app_states, uptime,
								    gps_time, battery_charge);
			if (transition) {
				task_terminate(i);
			}
		} else {
			transition = task_schedule_should_start(s, state, app_states, uptime,
								gps_time, battery_charge);
			if (transition) {
				task_start(i, uptime);
			}
		}
	}
}
