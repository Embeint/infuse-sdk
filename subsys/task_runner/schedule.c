/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/states.h>
#include <infuse/task_runner/schedule.h>

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
	if (schedule->validity >= _TASK_VALID_END) {
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
	if ((schedule->battery_start_threshold > 100) ||
	    (schedule->battery_terminate_threshold > 100)) {
		return false;
	}
	return true;
}

bool task_schedule_should_start(const struct task_schedule *schedule,
				struct task_schedule_state *state, atomic_t *app_states,
				uint32_t uptime, uint32_t epoch_time, uint8_t battery_soc)
{
	bool periodicity = true;
	bool battery = true;
	bool states;

	/* No tasks should be started when system is about to go down */
	if (atomic_test_bit(app_states, INFUSE_STATE_REBOOTING)) {
		return false;
	}

	if (schedule->periodicity_type == TASK_PERIODICITY_FIXED) {
		periodicity = (epoch_time % schedule->periodicity.fixed.period_s) == 0;
	}
	if (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) {
		periodicity = (uptime - state->last_run) >= schedule->periodicity.lockout.lockout_s;
	}
	battery = battery_soc >= schedule->battery_start_threshold;
	states = task_schedule_states_eval(&schedule->states_start, app_states, true);

	return periodicity && battery && states;
}

bool task_schedule_should_terminate(const struct task_schedule *schedule,
				    struct task_schedule_state *state, atomic_t *app_states,
				    uint32_t uptime, uint32_t epoch_time, uint8_t battery_soc)
{
	bool periodicity = false;
	bool battery = false;
	bool states;

	/* Tasks should be terminated when system is about to go down */
	if (atomic_test_bit(app_states, INFUSE_STATE_REBOOTING)) {
		return true;
	}

	if (schedule->timeout_s) {
		periodicity = state->runtime >= schedule->timeout_s;
	}
	if (schedule->battery_terminate_threshold) {
		battery = battery_soc <= schedule->battery_terminate_threshold;
	}
	states = task_schedule_states_eval(&schedule->states_terminate, app_states, false);

	return periodicity || battery || states;
}
