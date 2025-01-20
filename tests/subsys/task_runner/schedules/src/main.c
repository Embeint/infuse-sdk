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

#include <infuse/states.h>
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
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	/* Should always start and never stop */
	for (int i = 0; i < 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 50 + i,
							150 + i, 100));
		zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 30 + i,
							     100 + i, 100));
		state.runtime++;
	}
}

ZTEST(task_runner_schedules, test_periodicity_fixed)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 10,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 29, 100, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 30, 101, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 31, 102, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 32, 103, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 33, 104, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 34, 105, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 35, 106, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 36, 107, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 37, 108, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 38, 109, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 39, 110, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 40, 111, 100));
}

ZTEST(task_runner_schedules, test_periodicity_lockout)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
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
		zassert_false(task_schedule_should_start(&schedule, &state, app_states,
							 state.last_run + i, 10000 + i, 100));
	}
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, state.last_run + 12,
						100 + 12, 100));
}

ZTEST(task_runner_schedules, test_periodicity_after)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_AFTER,
	};
	struct task_schedule_state linked = {0};
	struct task_schedule_state state = {
		.linked = &linked,
	};

	zassert_true(task_schedule_validate(&schedule));

	/* Some small delay after termination */
	schedule.periodicity.after.duration_s = 10;
	linked.last_terminate = 20;
	for (int i = 0; i < 30; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}
	zassert_true(
		task_schedule_should_start(&schedule, &state, app_states, 30, 10000 + 30, 100));
	for (int i = 31; i < 60; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}

	/* Immediately after termination  */
	schedule.periodicity.after.duration_s = 0;
	linked.last_terminate = 100;
	for (int i = 0; i < 100; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}
	zassert_true(
		task_schedule_should_start(&schedule, &state, app_states, 100, 10000 + 30, 100));
	for (int i = 101; i < 120; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}

	/* Linked schedule not yet run */
	schedule.periodicity.after.duration_s = 10;
	linked.last_terminate = 0;
	for (int i = 0; i < 20; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}

	/* No linked schedule */
	state.linked = NULL;
	for (int i = 0; i < 20; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, i,
							 10000 + i, 100));
	}
}

ZTEST(task_runner_schedules, test_battery_static)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.battery_start_threshold = 50,
		.battery_terminate_threshold = 20,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	for (int i = 0; i < 50; i++) {
		zassert_false(
			task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 50; i <= 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}

	for (int i = 0; i <= 20; i++) {
		zassert_true(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 21; i <= 100; i++) {
		zassert_false(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}

	/* No battery constraints should start and not stop at 0% battery */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
	};

	zassert_true(task_schedule_validate(&schedule2));
	zassert_true(task_schedule_should_start(&schedule2, &state, app_states, 10, 100, 0));
	zassert_false(task_schedule_should_terminate(&schedule2, &state, app_states, 10, 100, 0));
}

#define TEST_ITER_STATES_START(schedule, state, app_states)                                        \
	task_schedule_should_start(schedule, state, app_states, 10, 100, 100)
#define TEST_ITER_STATES_TERMINATE(schedule, state, app_states)                                    \
	task_schedule_should_terminate(schedule, state, app_states, 10, 100, 100)

ZTEST(task_runner_schedules, test_app_states_basic)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(INFUSE_STATE_TIME_KNOWN),
		.states_terminate = TASK_STATES_DEFINE(INFUSE_STATE_TIME_KNOWN),
	};

	/* State not set, neither should pass */
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* State set, both should pass */
	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	atomic_clear_bit(app_states, INFUSE_STATE_TIME_KNOWN);
}

ZTEST(task_runner_schedules, test_app_states_timeout)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.states_start_timeout_2x_s = 10,
		.states_start = TASK_STATES_DEFINE(INFUSE_STATE_TIME_KNOWN),
	};
	struct task_schedule_state state = {
		.last_run = 100,
	};
	int uptime = 100;

	/* Up until T=119, state check should be failing */
	for (; uptime < 120; uptime++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, uptime,
							 10000 + uptime, 100));
	}
	/* After that, state check always passes */
	for (; uptime < (2 * UINT16_MAX); uptime++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, uptime,
							10000 + uptime, 100));
	}
}

ZTEST(task_runner_schedules, test_app_states_inverted)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(TR_NOT | INFUSE_STATE_TIME_KNOWN),
		.states_terminate = TASK_STATES_DEFINE(TR_NOT | INFUSE_STATE_TIME_KNOWN),
	};

	/* State not set, neither should pass */
	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* State set, both should pass */
	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	atomic_clear_bit(app_states, INFUSE_STATE_TIME_KNOWN);
}

ZTEST(task_runner_schedules, test_app_states_multiple)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, 20, 30, 40),
		.states_terminate = TASK_STATES_DEFINE(10, 20, 30, 40),
	};

	/* No states set */
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* Some states set */
	atomic_set_bit(app_states, 10);
	atomic_set_bit(app_states, 20);
	atomic_set_bit(app_states, 40);

	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* All states set */
	atomic_set_bit(app_states, 30);

	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
}

ZTEST(task_runner_schedules, test_app_states_multiple_inversions)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, TR_NOT | 20, 30, TR_NOT | 40),
		.states_terminate = TASK_STATES_DEFINE(10, TR_NOT | 20, 30, TR_NOT | 40),
	};

	/* No states set */
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* Two requested states */
	atomic_set_bit(app_states, 10);
	atomic_set_bit(app_states, 30);

	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));

	/* Not requested state */
	atomic_set_bit(app_states, 40);

	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
}

ZTEST(task_runner_schedules, test_timeout)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 15,
	};
	struct task_schedule_state state = {
		.runtime = 0,
	};

	zassert_true(task_schedule_validate(&schedule));

	for (int i = 0; i < 15; i++) {
		zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 30, 100,
							     100));
		state.runtime++;
	}
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 30, 100, 100));
}

ZTEST(task_runner_schedules, test_complex)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.timeout_s = 10,
		.battery_start_threshold = 50,
		.battery_terminate_threshold = 20,
		.states_start = TASK_STATES_DEFINE(INFUSE_STATE_TIME_KNOWN),
		.states_terminate = TASK_STATES_DEFINE(TR_NOT | INFUSE_STATE_TIME_KNOWN),
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 30,
	};
	struct task_schedule_state state = {
		.last_run = 100,
	};

	zassert_true(task_schedule_validate(&schedule));

	/* Does not start with battery below threshold */
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 200, 1000, 47));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 200, 1000, 48));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 200, 1000, 49));

	/* Does not start with lockout not passed */
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 110, 1000, 90));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 120, 1000, 90));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 129, 1000, 90));

	/* Does not start with no time knowledge */
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 130, 1000, 90));

	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);

	/* Starts with all valid */
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 130, 1000, 90));

	/* Does not stop by default */
	state.runtime = 2;
	zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 130, 1000, 90));

	/* Stops with only state loss */
	atomic_clear_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 130, 1000, 90));
	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);

	/* Stops with only battery below threshold */
	state.runtime = 5;
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 130, 1000, 19));

	/* Stops with only timeout passed */
	state.runtime = 10;
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 130, 1000, 90));

	/* Stops with all conditions */
	atomic_clear_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 130, 1000, 19));
}

ZTEST(task_runner_schedules, test_reboot_termination)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	/* Normally, task should always start and never terminate */
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 1000, 150, 100));
	zassert_false(
		task_schedule_should_terminate(&schedule, &state, app_states, 1000, 100, 100));

	/* Rebooting state should trigger task to terminate and not start */
	atomic_set_bit(app_states, INFUSE_STATE_REBOOTING);
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 1000, 150, 100));
	zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 1000, 100, 100));
}

ZTEST(task_runner_schedules, test_custom_args_included)
{
	struct task_schedule schedule;

	/* If custom arguments aren't included this won't compile */
	schedule.task_args.custom.custom1.arg1 = 0;
	schedule.task_args.custom.custom2.arg2 = INT32_MIN;
}

void test_init(void *fixture)
{
	infuse_state_clear(INFUSE_STATE_REBOOTING);
}

ZTEST_SUITE(task_runner_schedules, NULL, NULL, test_init, NULL, NULL);
