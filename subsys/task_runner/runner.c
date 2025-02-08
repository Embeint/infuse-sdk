/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include <infuse/work_q.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/task_runner/runner.h>

static const struct task_schedule *sch;
static struct task_schedule_state *sch_states;
static uint8_t sch_num;
static const struct task_config *tsk;
static struct task_data *tsk_states;
static uint8_t tsk_num;

LOG_MODULE_REGISTER(task_runner, CONFIG_TASK_RUNNER_LOG_LEVEL);

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
		} else {
			__ASSERT_NO_MSG(tsk[i].executor.thread.thread != NULL);
			__ASSERT_NO_MSG(tsk[i].executor.thread.task_fn != NULL);
			__ASSERT_NO_MSG(tsk[i].executor.thread.stack != NULL);
			__ASSERT_NO_MSG(tsk[i].executor.thread.stack_size > 128);
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
		sch_states[i].linked = NULL;
		/* Mark event callback as NULL */
		sch_states[i].event_cb = NULL;
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
		/* Link schedules together */
		if (sch[i].periodicity_type == TASK_PERIODICITY_AFTER) {
			if (sch[i].periodicity.after.schedule_idx >= num_schedules) {
				LOG_WRN("Schedule %d refers to index %d which does not exist", i,
					sch[i].periodicity.after.schedule_idx);
			} else {
				sch_states[i].linked =
					&sch_states[sch[i].periodicity.after.schedule_idx];
			}
		}
		/* Clear scheduling state */
		sch_states[i].last_run = 0;
		sch_states[i].last_terminate = 0;
		sch_states[i].runtime = 0;
	}
}

const struct task_schedule *task_schedule_from_data(struct task_data *data)
{
	return &sch[data->schedule_idx];
}

uint8_t *task_schedule_persistent_storage(struct task_data *data)
{
	return sch_states[data->schedule_idx].runtime_state;
}

void task_workqueue_reschedule(struct task_data *task, k_timeout_t delay)
{
	unsigned int signaled;
	int rc;

	/* Override delay if task requested to terminate */
	k_poll_signal_check(&task->terminate_signal, &signaled, &rc);
	if (signaled == 1) {
		delay = K_NO_WAIT;
	}
	/* Increment reschedule count */
	task->executor.workqueue.reschedule_counter += 1;
	/* Reschedule on queue */
	infuse_work_reschedule(&task->executor.workqueue.work, delay);
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
		tid = k_thread_create(c->executor.thread.thread, c->executor.thread.stack,
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
		infuse_work_schedule(&d->executor.workqueue.work, K_NO_WAIT);
	}
	if (state->event_cb) {
		state->event_cb(s, TASK_SCHEDULE_STARTED);
	}
}

static void task_terminate(uint8_t schedule_index)
{
	const struct task_schedule *s = &sch[schedule_index];
	struct task_schedule_state *state = &sch_states[schedule_index];
	const struct task_config *c = &tsk[state->task_idx];
	struct task_data *d = &tsk_states[state->task_idx];

	LOG_DBG("Requesting task %s to terminate", c->name);
	/* Raise the termination signal */
	k_poll_signal_raise(&d->terminate_signal, 0);
	if (c->exec_type == TASK_EXECUTOR_WORKQUEUE) {
		/* Reschedule immediately to terminate */
		infuse_work_reschedule(&d->executor.workqueue.work, K_NO_WAIT);
	}
	if (state->event_cb) {
		state->event_cb(s, TASK_SCHEDULE_TERMINATE_REQUEST);
	}
}

static bool task_has_terminated(uint8_t task_idx)
{
	const struct task_config *c = &tsk[task_idx];
	struct task_data *d = &tsk_states[task_idx];
	const struct task_schedule *s = &sch[d->schedule_idx];
	struct task_schedule_state *state = &sch_states[d->schedule_idx];

	if (c->exec_type == TASK_EXECUTOR_THREAD) {
		if (k_thread_join(c->executor.thread.thread, K_NO_WAIT) != 0) {
			return false;
		}
	} else {
		__ASSERT_NO_MSG(c->exec_type == TASK_EXECUTOR_WORKQUEUE);
		if (k_work_busy_get(&d->executor.workqueue.work.work) != 0) {
			return false;
		}
	}
	if (state->event_cb) {
		state->event_cb(s, TASK_SCHEDULE_STOPPED);
	}
	return true;
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
				LOG_DBG("Task %s terminated @ %d", tsk[i].name, uptime);
				sch_states[tsk_states[i].schedule_idx].last_terminate = uptime;
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

		/* Start/restart permanently running tasks */
		if (s->validity == TASK_VALID_PERMANENTLY_RUNS) {
			if (!d->running) {
				task_start(i, uptime);
			}
			sch_states[i].runtime += 1;
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

uint8_t task_runner_watchdog_channel(void)
{
	return wdog_channel;
}
