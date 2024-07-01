/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/logging/log.h>

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

static void task_start(uint8_t schedule_index, uint32_t uptime)
{
	const struct task_schedule *s = &sch[schedule_index];
	struct task_schedule_state *state = &sch_states[schedule_index];
	const struct task_config *c = &tsk[state->task_idx];
	struct task_data *d = &tsk_states[state->task_idx];

	LOG_INF("Booting task %s from schedule %d", c->name, schedule_index);

	/* Initialise state information */
	state->last_run = uptime;
	state->runtime = 0;
	d->running = true;
	d->schedule_idx = schedule_index;

	k_poll_signal_init(&d->terminate_signal);

	/* Boot the thread */
	(void)k_thread_create(&d->thread, c->thread_stack, c->thread_stack_size,
			      (k_thread_entry_t)c->task_fn, (void *)s, &d->terminate_signal, NULL,
			      5, 0, K_NO_WAIT);
}

static void task_terminate(uint8_t schedule_index)
{
	struct task_schedule_state *state = &sch_states[schedule_index];
	const struct task_config *c = &tsk[state->task_idx];
	struct task_data *d = &tsk_states[state->task_idx];

	LOG_INF("Requesting task %s to terminate", c->name);
	/* Raise the termination signal */
	k_poll_signal_raise(&d->terminate_signal, 0);
}

void task_runner_iterate(uint32_t uptime, uint32_t gps_time, uint8_t battery_charge)
{
	bool transition;

	/* Determine if any running tasks have terminated */
	for (int i = 0; i < tsk_num; i++) {
		if (tsk_states[i].running) {
			if (k_thread_join(&tsk_states[i].thread, K_NO_WAIT) == 0) {
				LOG_INF("Task %s terminated", tsk[i].name);
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

		/* Is referred task running due to a different schedule */
		if (d->running && (d->schedule_idx != i)) {
			LOG_DBG("Not evaluating %d as started from %d", i, d->schedule_idx);
			continue;
		}

		/* Should any should change state */
		if (d->running) {
			sch_states[i].runtime += 1;
			transition = task_schedule_should_terminate(s, state, uptime, gps_time,
								    battery_charge);
			if (transition) {
				task_terminate(i);
			}
		} else {
			transition = task_schedule_should_start(s, state, uptime, gps_time,
								battery_charge);
			if (transition) {
				task_start(i, uptime);
			}
		}
	}
}
