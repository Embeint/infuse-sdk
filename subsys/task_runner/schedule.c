/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/sys/clock.h>

#include <infuse/math/common.h>
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
	bool result;
	bool or_cond;
	int i;

	/* No states to evaluate, return default value */
	if (states->states[0] == 0) {
		return fallthrough;
	}

	/* Setup initial value depending on whether OR is set on S0
	 * in order to cancel its effect.
	 */
	result = !(states->metadata & (BIT(4)));

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
		or_cond = !!(states->metadata & BIT(i + 4));
		if (or_cond) {
			result |= state_val;
		} else {
			result &= state_val;
		}
	}
	/* Final result */
	return result;
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
	if (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT_DYNAMIC_BATTERY) {
		const struct periodicity_lockout_dynamic_battery *ldb =
			&schedule->periodicity.lockout_dynamic_battery;

		if ((ldb->battery_min >= ldb->battery_max) || (ldb->lockout_min == 0) ||
		    (ldb->lockout_max == 0)) {
			return false;
		}
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
	const struct periodicity_lockout_dynamic_battery *ldb;
	uint8_t validity_masked = schedule->validity & _TASK_VALID_MASK;
	uint32_t since_last_run = uptime - state->last_run;
	uint32_t lockout_val;
	bool is_active = atomic_test_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	bool periodicity = true;
	bool battery_lower;
	bool battery_upper;
	bool ignore_first;
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

	/* Boot lockout */
	if (schedule->boot_lockout_minutes &&
	    ((uptime / SEC_PER_MIN) < schedule->boot_lockout_minutes)) {
		return false;
	}

	if (schedule->periodicity_type == TASK_PERIODICITY_FIXED) {
		periodicity = (epoch_time % schedule->periodicity.fixed.period_s) == 0;
	} else if ((schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) ||
		   (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT_DYNAMIC_BATTERY)) {
		ldb = &schedule->periodicity.lockout_dynamic_battery;
		if (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) {
			lockout_val = schedule->periodicity.lockout.lockout_s;
		} else if (battery_soc <= ldb->battery_min) {
			lockout_val = ldb->lockout_min;
		} else if (battery_soc >= ldb->battery_max) {
			lockout_val = ldb->lockout_max;
		} else {
			/*Linear scale depending on SoC */
			lockout_val = math_2d_linear_interpolate_fast(
				ldb->battery_min, ldb->battery_max, ldb->lockout_min,
				ldb->lockout_max, battery_soc);
		}

		ignore_first = lockout_val & TASK_RUNNER_LOCKOUT_IGNORE_FIRST;
		lockout_val &= TASK_RUNNER_LOCKOUT_VALUE_MASK;
		periodicity = since_last_run >= lockout_val;
		/* Valid if TASK_RUNNER_LOCKOUT_IGNORE_FIRST set, schedule has not yet run, and
		 * uptime is not 0 (we need state->last_run to end up a non-zero value)
		 */
		if (ignore_first && (state->last_run == 0) && uptime) {
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
