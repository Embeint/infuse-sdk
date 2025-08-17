/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/states.h>
#include <infuse/task_runner/schedule.h>

#define TASK_RUNNER_LOCKOUT_VALUE_MASK ~TASK_RUNNER_LOCKOUT_IGNORE_FIRST

#ifdef CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS

/* Validation of custom arguments */
BUILD_ASSERT(sizeof(union custom_task_arguments) <= 16, "Custom arguments too large");
BUILD_ASSERT(__alignof(union custom_task_arguments) == 1,
	     "Custom arguments require unsupported alignment");

#endif /* CONFIG_TASK_RUNNER_CUSTOM_TASK_DEFINITIONS */

static bool task_schedule_states_eval(const struct task_schedule_state_conditions *states,
				      atomic_t *app_states, bool fallthrough)
{
	uint8_t state_idx;
	bool state_val;
	int i;

	/* No states to evaluate, return default value */
	if (states->states[0] == 0) {
		return fallthrough;
	}

	for (i = 0; i < ARRAY_SIZE(states->states); i++) {
		state_idx = states->states[i];
		if (state_idx == 0) {
			break;
		}
		state_val = atomic_test_bit(app_states, state_idx);
		/* Invert if required */
		if (states->metadata & BIT(i)) {
			state_val = !state_val;
		}
		/* Fail if not set */
		if (!state_val) {
			return false;
		}
	}
	/* No states failed evaluation */
	return true;
}

bool task_schedule_validate(const struct task_schedule *schedule)
{
	uint8_t validity_masked = schedule->validity & _TASK_VALID_MASK;

	if (validity_masked == 0) {
		return false;
	}
	if (validity_masked >= _TASK_VALID_END) {
		return false;
	}
	if (schedule->periodicity_type >= _TASK_PERIODICITY_END) {
		return false;
	}
	if ((schedule->periodicity_type == TASK_PERIODICITY_FIXED) &&
	    (schedule->periodicity.fixed.period_s == 0)) {
		return false;
	}
	if ((schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) &&
	    (schedule->periodicity.lockout.lockout_s == 0)) {
		return false;
	}
	if ((schedule->battery_start.lower > 100) || (schedule->battery_start.upper > 100) ||
	    (schedule->battery_terminate.lower > 100) ||
	    (schedule->battery_terminate.upper > 100)) {
		return false;
	}
	if (schedule->battery_start.lower && schedule->battery_start.upper &&
	    (schedule->battery_start.upper <= schedule->battery_start.lower)) {
		return false;
	}
	if (schedule->battery_terminate.lower && schedule->battery_terminate.upper &&
	    (schedule->battery_terminate.upper <= schedule->battery_terminate.lower)) {
		return false;
	}
	return true;
}

bool task_schedule_should_start(const struct task_schedule *schedule,
				struct task_schedule_state *state, atomic_t *app_states,
				uint32_t uptime, uint32_t epoch_time, uint8_t battery_soc)
{
	uint8_t validity_masked = schedule->validity & _TASK_VALID_MASK;
	uint32_t since_last_run = uptime - state->last_run;
	uint32_t lockout_val;
	bool is_active = atomic_test_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	bool periodicity = true;
	bool battery_lower;
	bool battery_upper;
	bool states;

	/* No tasks should be started when system is about to go down */
	if (atomic_test_bit(app_states, INFUSE_STATE_REBOOTING)) {
		return false;
	}

	/* Valdity based on application state */
	if ((validity_masked == TASK_VALID_ACTIVE) && !is_active) {
		return false;
	}
	if ((validity_masked == TASK_VALID_INACTIVE) && is_active) {
		return false;
	}

	if (schedule->periodicity_type == TASK_PERIODICITY_FIXED) {
		periodicity = (epoch_time % schedule->periodicity.fixed.period_s) == 0;
	} else if (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) {
		lockout_val =
			schedule->periodicity.lockout.lockout_s & TASK_RUNNER_LOCKOUT_VALUE_MASK;
		periodicity = since_last_run >= lockout_val;
		/* Valid if TASK_RUNNER_LOCKOUT_IGNORE_FIRST set, schedule has not yet run, and
		 * uptime is not 0 (we need state->last_run to end up a non-zero value)
		 */
		if (schedule->periodicity.lockout.lockout_s & TASK_RUNNER_LOCKOUT_IGNORE_FIRST &&
		    (state->last_run == 0) && uptime) {
			periodicity = true;
		}
	} else if (schedule->periodicity_type == TASK_PERIODICITY_AFTER) {
		periodicity = state->linked && state->linked->last_terminate &&
			      ((state->linked->last_terminate +
				schedule->periodicity.after.duration_s) == uptime);
	}

	battery_lower = (schedule->battery_start.lower == 0) ||
			(battery_soc >= schedule->battery_start.lower);
	battery_upper = (schedule->battery_start.upper == 0) ||
			(battery_soc <= schedule->battery_start.upper);
	states = (schedule->states_start_timeout_2x_s &&
		  (since_last_run >= (2 * schedule->states_start_timeout_2x_s))) ||
		 task_schedule_states_eval(&schedule->states_start, app_states, true);

	return periodicity && battery_lower && battery_upper && states;
}

bool task_schedule_should_terminate(const struct task_schedule *schedule,
				    struct task_schedule_state *state, atomic_t *app_states,
				    uint32_t uptime, uint32_t epoch_time, uint8_t battery_soc)
{
	uint8_t validity_masked = schedule->validity & _TASK_VALID_MASK;
	bool is_active = atomic_test_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	bool periodicity = false;
	bool battery_lower;
	bool battery_upper;
	bool states;

	/* Tasks should be terminated when system is about to go down */
	if (atomic_test_bit(app_states, INFUSE_STATE_REBOOTING)) {
		return true;
	}

	/* Valdity based on application state */
	if ((validity_masked == TASK_VALID_ACTIVE) && !is_active) {
		return true;
	}
	if ((validity_masked == TASK_VALID_INACTIVE) && is_active) {
		return true;
	}

	if (schedule->timeout_s) {
		periodicity = state->runtime >= schedule->timeout_s;
	}
	battery_lower = (schedule->battery_terminate.lower != 0) &&
			(battery_soc <= schedule->battery_terminate.lower);
	battery_upper = (schedule->battery_terminate.upper != 0) &&
			(battery_soc >= schedule->battery_terminate.upper);
	states = task_schedule_states_eval(&schedule->states_terminate, app_states, false);

	return periodicity || battery_lower || battery_upper || states;
}
