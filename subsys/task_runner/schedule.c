/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/task_runner/schedule.h>

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
				struct task_schedule_state *state, uint32_t uptime,
				uint32_t epoch_time, uint8_t battery_soc)
{
	bool periodicity = true;
	bool battery = true;

	if (schedule->periodicity_type == TASK_PERIODICITY_FIXED) {
		periodicity = (epoch_time % schedule->periodicity.fixed.period_s) == 0;
	}
	if (schedule->periodicity_type == TASK_PERIODICITY_LOCKOUT) {
		periodicity = (uptime - state->last_run) >= schedule->periodicity.lockout.lockout_s;
	}
	battery = battery_soc >= schedule->battery_start_threshold;

	return periodicity && battery;
}

bool task_schedule_should_terminate(const struct task_schedule *schedule,
				    struct task_schedule_state *state, uint32_t uptime,
				    uint32_t epoch_time, uint8_t battery_soc)
{
	bool periodicity = false;
	bool battery = false;

	if (schedule->timeout_s) {
		periodicity = state->runtime >= schedule->timeout_s;
	}
	if (schedule->battery_terminate_threshold) {
		battery = battery_soc <= schedule->battery_terminate_threshold;
	}
	return periodicity || battery;
}
