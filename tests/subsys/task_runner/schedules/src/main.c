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

#include <infuse/task_runner/schedule.h>

ZTEST(task_runner_schedules, test_validate_schedules)
{
	struct task_schedule invalid1 = {
		.validity = _TASK_VALID_END,
	};
	struct task_schedule invalid2 = {
		.periodicity_type = _TASK_PERIODICITY_END,
	};
	struct task_schedule invalid3 = {
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 0,
	};
	struct task_schedule invalid4 = {
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 0,
	};
	struct task_schedule invalid5 = {
		.battery_start_threshold = 101,
	};
	struct task_schedule invalid6 = {
		.battery_terminate_threshold = 101,
	};

	zassert_false(task_schedule_validate(&invalid1));
	zassert_false(task_schedule_validate(&invalid2));
	zassert_false(task_schedule_validate(&invalid3));
	zassert_false(task_schedule_validate(&invalid4));
	zassert_false(task_schedule_validate(&invalid5));
	zassert_false(task_schedule_validate(&invalid6));
}

ZTEST(task_runner_schedules, test_empty_schedule)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	/* Should always start and never stop */
	for (int i = 0; i < 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, 50 + i, 150 + i, 100));
		zassert_false(
			task_schedule_should_terminate(&schedule, &state, 30 + i, 100 + i, 100));
		state.runtime++;
	}
}

ZTEST(task_runner_schedules, test_periodicity_fixed)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	zassert_true(task_schedule_should_start(&schedule, &state, 29, 100, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 30, 101, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 31, 102, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 32, 103, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 33, 104, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 34, 105, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 35, 106, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 36, 107, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 37, 108, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 38, 109, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, 39, 110, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, 40, 111, 100));
}

ZTEST(task_runner_schedules, test_periodicity_lockout)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 12,
	};
	struct task_schedule_state state = {
		.last_run = 20,
	};

	zassert_true(task_schedule_validate(&schedule));

	for (int i = 0; i < 12; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, state.last_run + i,
							 10000 + i, 100));
	}
	zassert_true(
		task_schedule_should_start(&schedule, &state, state.last_run + 12, 100 + 12, 100));
}

ZTEST(task_runner_schedules, test_battery_static)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.battery_start_threshold = 50,
		.battery_terminate_threshold = 20,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	for (int i = 0; i < 50; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, 10, 100, i));
	}
	for (int i = 50; i <= 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, 10, 100, i));
	}

	for (int i = 0; i <= 20; i++) {
		zassert_true(task_schedule_should_terminate(&schedule, &state, 10, 100, i));
	}
	for (int i = 21; i <= 100; i++) {
		zassert_false(task_schedule_should_terminate(&schedule, &state, 10, 100, i));
	}

	/* No battery constraints should start and not stop at 0% battery */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
	};

	zassert_true(task_schedule_validate(&schedule2));
	zassert_true(task_schedule_should_start(&schedule2, &state, 10, 100, 0));
	zassert_false(task_schedule_should_terminate(&schedule2, &state, 10, 100, 0));
}

ZTEST(task_runner_schedules, test_timeout)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 15,
	};
	struct task_schedule_state state = {
		.runtime = 0,
	};

	zassert_true(task_schedule_validate(&schedule));

	for (int i = 0; i < 15; i++) {
		zassert_false(task_schedule_should_terminate(&schedule, &state, 30, 100, 100));
		state.runtime++;
	}
	zassert_true(task_schedule_should_terminate(&schedule, &state, 30, 100, 100));
}

ZTEST(task_runner_schedules, test_complex)
{
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 10,
		.battery_start_threshold = 50,
		.battery_terminate_threshold = 20,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 30,
	};
	struct task_schedule_state state = {
		.last_run = 100,
	};

	zassert_true(task_schedule_validate(&schedule));

	/* Does not start with battery below threshold */
	zassert_false(task_schedule_should_start(&schedule, &state, 200, 1000, 47));
	zassert_false(task_schedule_should_start(&schedule, &state, 200, 1000, 48));
	zassert_false(task_schedule_should_start(&schedule, &state, 200, 1000, 49));

	/* Does not start with lockout not passed */
	zassert_false(task_schedule_should_start(&schedule, &state, 110, 1000, 90));
	zassert_false(task_schedule_should_start(&schedule, &state, 120, 1000, 90));
	zassert_false(task_schedule_should_start(&schedule, &state, 129, 1000, 90));

	/* Starts with both valid */
	zassert_true(task_schedule_should_start(&schedule, &state, 130, 1000, 90));

	/* Stops with only battery below threshold */
	state.runtime = 5;
	zassert_true(task_schedule_should_terminate(&schedule, &state, 130, 1000, 19));

	/* Stops with only timeout passed */
	state.runtime = 10;
	zassert_true(task_schedule_should_terminate(&schedule, &state, 130, 1000, 90));

	/* Stops with both conditions */
	zassert_true(task_schedule_should_terminate(&schedule, &state, 130, 1000, 19));
}

ZTEST_SUITE(task_runner_schedules, NULL, NULL, NULL, NULL, NULL);
