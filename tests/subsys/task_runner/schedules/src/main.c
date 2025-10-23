/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/states.h>
#include <infuse/task_runner/schedule.h>

ZTEST(task_runner_schedules, test_schedules_states_define)
{
	struct task_schedule schedules1[2];
	struct task_schedule schedules2[15];
	struct task_schedule schedules3[63];

	TASK_SCHEDULE_STATES_DEFINE(test_states1, schedules1);
	TASK_SCHEDULE_STATES_DEFINE(test_states2, schedules2);
	TASK_SCHEDULE_STATES_DEFINE(test_states3, schedules3);

	(void)schedules1;
	(void)schedules2;
	(void)schedules3;

	/* Sized to the default schedule array */
	zassert_equal(ARRAY_SIZE(schedules1), ARRAY_SIZE(test_states1));
	zassert_equal(ARRAY_SIZE(schedules2), ARRAY_SIZE(test_states2));
	zassert_equal(ARRAY_SIZE(schedules3), ARRAY_SIZE(test_states3));
}

ZTEST(task_runner_schedules, test_validate_schedules)
{
	struct task_schedule invalid1 = {0};
	struct task_schedule invalid2 = {
		.validity = _TASK_VALID_END,
	};
	struct task_schedule invalid3 = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = _TASK_PERIODICITY_END,
	};
	struct task_schedule invalid4 = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_FIXED,
		.periodicity.fixed.period_s = 0,
	};
	struct task_schedule invalid5 = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = 0,
	};
	struct task_schedule invalid6 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_start.lower = 101,
	};
	struct task_schedule invalid7 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_start.upper = 101,
	};
	struct task_schedule invalid8 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_terminate.lower = 101,
	};
	struct task_schedule invalid9 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_terminate.upper = 101,
	};
	struct task_schedule invalid10 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_start.lower = 70,
		.battery_start.upper = 60,
	};
	struct task_schedule invalid11 = {
		.validity = TASK_VALID_ALWAYS,
		.battery_terminate.lower = 70,
		.battery_terminate.upper = 60,
	};

	zassert_false(task_schedule_validate(&invalid1));
	zassert_false(task_schedule_validate(&invalid2));
	zassert_false(task_schedule_validate(&invalid3));
	zassert_false(task_schedule_validate(&invalid4));
	zassert_false(task_schedule_validate(&invalid5));
	zassert_false(task_schedule_validate(&invalid6));
	zassert_false(task_schedule_validate(&invalid7));
	zassert_false(task_schedule_validate(&invalid8));
	zassert_false(task_schedule_validate(&invalid9));
	zassert_false(task_schedule_validate(&invalid10));
	zassert_false(task_schedule_validate(&invalid11));
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

ZTEST(task_runner_schedules, test_locked_schedule)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_LOCKED | TASK_VALID_ALWAYS,
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

ZTEST(task_runner_schedules, test_active_schedule)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ACTIVE,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	/* While active, should always start and never stop */
	atomic_set_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	for (int i = 0; i < 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 50 + i,
							150 + i, 100));
		zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 30 + i,
							     100 + i, 100));
		state.runtime++;
	}

	/* While inactive, should never start and always stop */
	atomic_clear_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	for (int i = 0; i < 100; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, 50 + i,
							 150 + i, 100));
		zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 30 + i,
							    100 + i, 100));
		state.runtime++;
	}
}

ZTEST(task_runner_schedules, test_inactive_schedule)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_INACTIVE,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	/* While active, should never start and always stop */
	atomic_set_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	for (int i = 0; i < 100; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states, 50 + i,
							 150 + i, 100));
		zassert_true(task_schedule_should_terminate(&schedule, &state, app_states, 30 + i,
							    100 + i, 100));
		state.runtime++;
	}

	/* While inactive, should always start and never stop */
	atomic_clear_bit(app_states, INFUSE_STATE_APPLICATION_ACTIVE);
	for (int i = 0; i < 100; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 50 + i,
							150 + i, 100));
		zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 30 + i,
							     100 + i, 100));
		state.runtime++;
	}
}

ZTEST(task_runner_schedules, test_boot_lockout)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.boot_lockout_minutes = 2,
	};
	struct task_schedule_state state = {0};

	zassert_true(task_schedule_validate(&schedule));

	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 0, 100, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 100, 101, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 118, 102, 100));
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, 119, 103, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 120, 104, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 121, 105, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 123, 106, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 1000, 107, 100));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 1000000, 108, 100));
	zassert_true(
		task_schedule_should_start(&schedule, &state, app_states, UINT32_MAX, 109, 100));
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

ZTEST(task_runner_schedules, test_periodicity_lockout_ignore_boot)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule schedule = {
		.validity = TASK_VALID_ALWAYS,
		.periodicity_type = TASK_PERIODICITY_LOCKOUT,
		.periodicity.lockout.lockout_s = TASK_RUNNER_LOCKOUT_IGNORE_FIRST | 100,
	};
	struct task_schedule_state state = {
		.last_run = 0,
	};

	zassert_true(task_schedule_validate(&schedule));

	/* Doesn't run at uptime 0 */
	zassert_false(task_schedule_should_start(&schedule, &state, app_states, state.last_run + 0,
						 10000 + 0, 100));
	/* Periodicity check always passes before first run */
	for (int i = 1; i < 150; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states,
							state.last_run + i, 10000 + i, 100));
	}

	/* After running once, behaves as per normal */
	state.last_run = 10;
	for (int i = 0; i < 100; i++) {
		zassert_false(task_schedule_should_start(&schedule, &state, app_states,
							 state.last_run + i, 10000 + i, 100));
	}
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, state.last_run + 100,
						100 + 100, 100));
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
		.battery_start.lower = 50,
		.battery_terminate.lower = 20,
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

	/* Add upper start threshold */
	schedule.battery_start.upper = 60;
	zassert_true(task_schedule_validate(&schedule));
	for (int i = 0; i < 50; i++) {
		zassert_false(
			task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 50; i <= 60; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 61; i <= 100; i++) {
		zassert_false(
			task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}

	/* Remove lower start threshold */
	schedule.battery_start.lower = 0;
	zassert_true(task_schedule_validate(&schedule));
	for (int i = 0; i <= 60; i++) {
		zassert_true(task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 61; i <= 100; i++) {
		zassert_false(
			task_schedule_should_start(&schedule, &state, app_states, 10, 100, i));
	}

	/* Add upper terminate threshold */
	schedule.battery_terminate.upper = 60;
	zassert_true(task_schedule_validate(&schedule));
	for (int i = 0; i <= 20; i++) {
		zassert_true(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 21; i <= 59; i++) {
		zassert_false(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 60; i <= 100; i++) {
		zassert_true(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}

	/* Remove lower terminate threshold */
	schedule.battery_terminate.lower = 0;
	zassert_true(task_schedule_validate(&schedule));
	for (int i = 0; i <= 59; i++) {
		zassert_false(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}
	for (int i = 60; i <= 100; i++) {
		zassert_true(
			task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, i));
	}

	/* No battery constraints should start and not stop at 0% battery */
	schedule.battery_start.lower = 0;
	schedule.battery_start.upper = 0;
	schedule.battery_terminate.lower = 0;
	schedule.battery_terminate.upper = 0;

	zassert_true(task_schedule_validate(&schedule));
	zassert_true(task_schedule_should_start(&schedule, &state, app_states, 10, 100, 0));
	zassert_false(task_schedule_should_terminate(&schedule, &state, app_states, 10, 100, 0));
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
	/* TR_OR has no effect on first state */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(TR_OR | INFUSE_STATE_TIME_KNOWN),
		.states_terminate = TASK_STATES_DEFINE(TR_OR | INFUSE_STATE_TIME_KNOWN),
	};

	/* State not set, neither should pass */
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_START(&schedule2, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states));

	/* State set, both should pass */
	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_START(&schedule2, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states));
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
	/* TR_OR has no effect on first state */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(TR_NOT | TR_OR | INFUSE_STATE_TIME_KNOWN),
		.states_terminate = TASK_STATES_DEFINE(TR_NOT | TR_OR | INFUSE_STATE_TIME_KNOWN),
	};

	/* State not set, neither should pass */
	zassert_true(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	zassert_true(TEST_ITER_STATES_START(&schedule2, &state, app_states));
	zassert_true(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states));

	/* State set, both should pass */
	atomic_set_bit(app_states, INFUSE_STATE_TIME_KNOWN);
	zassert_false(TEST_ITER_STATES_START(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule, &state, app_states));
	zassert_false(TEST_ITER_STATES_START(&schedule2, &state, app_states));
	zassert_false(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states));
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

ZTEST(task_runner_schedules, test_app_states_multiple_or)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	/* (((10 || 20) && 30) && 40) */
	struct task_schedule schedule1 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, TR_OR | 20, 30, 40),
		.states_terminate = TASK_STATES_DEFINE(10, TR_OR | 20, 30, 40),
	};
	/* (((10 || 20) && 30) || 40) */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, TR_OR | 20, 30, TR_OR | 40),
		.states_terminate = TASK_STATES_DEFINE(10, TR_OR | 20, 30, TR_OR | 40),
	};
	/* (((10 && 20) || 30) && 40) */
	struct task_schedule schedule3 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, 20, TR_OR | 30, 40),
		.states_terminate = TASK_STATES_DEFINE(10, 20, TR_OR | 30, 40),
	};

	/* Exhaustively test combinations  */
	for (int i = 0; i < 16; i++) {
		bool s0 = i & BIT(0);
		bool s1 = i & BIT(1);
		bool s2 = i & BIT(2);
		bool s3 = i & BIT(3);

		atomic_set_bit_to(app_states, 10, s0);
		atomic_set_bit_to(app_states, 20, s1);
		atomic_set_bit_to(app_states, 30, s2);
		atomic_set_bit_to(app_states, 40, s3);

		zassert_equal(TEST_ITER_STATES_START(&schedule1, &state, app_states),
			      (((s0 || s1) && s2) && s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule1, &state, app_states),
			      (((s0 || s1) && s2) && s3));
		zassert_equal(TEST_ITER_STATES_START(&schedule2, &state, app_states),
			      (((s0 || s1) && s2) || s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states),
			      (((s0 || s1) && s2) || s3));
		zassert_equal(TEST_ITER_STATES_START(&schedule3, &state, app_states),
			      (((s0 && s1) || s2) && s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule3, &state, app_states),
			      (((s0 && s1) || s2) && s3));
	}
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

ZTEST(task_runner_schedules, test_app_states_multiple_inversions_or)
{
	INFUSE_STATES_ARRAY(app_states) = {0};
	struct task_schedule_state state = {0};
	/* (((10 || 20) && !30) && 40) */
	struct task_schedule schedule1 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, TR_OR | 20, TR_NOT | 30, 40),
		.states_terminate = TASK_STATES_DEFINE(10, TR_OR | 20, TR_NOT | 30, 40),
	};
	/* (((!10 || 20) && 30) || 40) */
	struct task_schedule schedule2 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(TR_NOT | 10, TR_OR | 20, 30, TR_OR | 40),
		.states_terminate = TASK_STATES_DEFINE(TR_NOT | 10, TR_OR | 20, 30, TR_OR | 40),
	};
	/* (((10 && !20) || !30) && 40) */
	struct task_schedule schedule3 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start = TASK_STATES_DEFINE(10, TR_NOT | 20, TR_NOT | TR_OR | 30, 40),
		.states_terminate = TASK_STATES_DEFINE(10, TR_NOT | 20, TR_NOT | TR_OR | 30, 40),
	};
	/* (((10 || !20) && 30) || !40) */
	struct task_schedule schedule4 = {
		.validity = TASK_VALID_ALWAYS,
		.states_start =
			TASK_STATES_DEFINE(10, TR_OR | TR_NOT | 20, 30, TR_OR | TR_NOT | 40),
		.states_terminate =
			TASK_STATES_DEFINE(10, TR_OR | TR_NOT | 20, 30, TR_OR | TR_NOT | 40),
	};

	/* Exhaustively test combinations  */
	for (int i = 0; i < 16; i++) {
		bool s0 = i & BIT(0);
		bool s1 = i & BIT(1);
		bool s2 = i & BIT(2);
		bool s3 = i & BIT(3);

		atomic_set_bit_to(app_states, 10, s0);
		atomic_set_bit_to(app_states, 20, s1);
		atomic_set_bit_to(app_states, 30, s2);
		atomic_set_bit_to(app_states, 40, s3);

		zassert_equal(TEST_ITER_STATES_START(&schedule1, &state, app_states),
			      (((s0 || s1) && !s2) && s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule1, &state, app_states),
			      (((s0 || s1) && !s2) && s3));
		zassert_equal(TEST_ITER_STATES_START(&schedule2, &state, app_states),
			      (((!s0 || s1) && s2) || s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule2, &state, app_states),
			      (((!s0 || s1) && s2) || s3));
		zassert_equal(TEST_ITER_STATES_START(&schedule3, &state, app_states),
			      (((s0 && !s1) || !s2) && s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule3, &state, app_states),
			      (((s0 && !s1) || !s2) && s3));
		zassert_equal(TEST_ITER_STATES_START(&schedule4, &state, app_states),
			      (((s0 || !s1) && s2) || !s3));
		zassert_equal(TEST_ITER_STATES_TERMINATE(&schedule4, &state, app_states),
			      (((s0 || !s1) && s2) || !s3));
	}
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
		.battery_start.lower = 50,
		.battery_terminate.lower = 20,
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
