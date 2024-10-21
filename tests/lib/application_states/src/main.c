/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <stdint.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/states.h>

ZTEST(application_states, test_basic)
{
	zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATES_END));

	infuse_state_set(INFUSE_STATE_REBOOTING);

	zassert_true(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATES_END));

	infuse_state_set(INFUSE_STATES_END);

	zassert_true(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_true(infuse_state_get(INFUSE_STATES_END));

	infuse_state_clear(INFUSE_STATE_REBOOTING);

	zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_true(infuse_state_get(INFUSE_STATES_END));

	infuse_state_clear(INFUSE_STATES_END);

	zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATES_END));
}

ZTEST(application_states, test_state_timeout_basic)
{
	INFUSE_STATES_ARRAY(states);

	zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATES_END));

	/* No timeout, no state */
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 0);
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));

	/* Timeout of 1 second */
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 1);
	zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	infuse_states_snapshot(states);
	infuse_states_tick(states);
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));

	/* Timeout of 17 seconds */
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 17);
	for (int i = 0; i < 17; i++) {
		zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
		infuse_states_snapshot(states);
		infuse_states_tick(states);
	}
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
}

ZTEST(application_states, test_state_timeout_snapshot)
{
	INFUSE_STATES_ARRAY(states) = {0};

	zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	zassert_false(infuse_state_get(INFUSE_STATES_END));

	/* Timeout of 1 second */
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 1);

	/* Iterate, but pretend that the state was NOT set at the time of snapshotting */
	infuse_states_tick(states);

	/* State should still be set */
	zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));

	/* But after the next run with snapshotting, cleared */
	infuse_states_snapshot(states);
	infuse_states_tick(states);
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
}

ZTEST(application_states, test_state_timeout_many)
{
	INFUSE_STATES_ARRAY(states);

	/* Start many timeouts */
	for (int i = 0; i < CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS; i++) {
		infuse_state_set_timeout(20 + 2 * i, 1000 + i);
		zassert_true(infuse_state_get(20 + 2 * i));
	}
	/* Start one too many (should not be set) */
	infuse_state_set_timeout(0, 10);
	zassert_false(infuse_state_get(0));

	/* First 1000 ticks all set */
	for (int i = 0; i < 999; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		for (int i = 0; i < CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS; i++) {
			zassert_true(infuse_state_get(20 + 2 * i));
		}
	}

	/* Each state should timeout on the next tick */
	for (int i = 0; i < CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_false(infuse_state_get(20 + 2 * i));
	}

	/* All should be false now */
	for (int i = 0; i < CONFIG_INFUSE_APPLICATION_STATES_MAX_TIMEOUTS; i++) {
		zassert_false(infuse_state_get(20 + 2 * i));
	}
}

ZTEST(application_states, test_state_clear_timeout_remove)
{
	INFUSE_STATES_ARRAY(states);

	/* Clearing a pending timeout should remove any timeout state */
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 5);
	infuse_state_clear(INFUSE_STATE_TIME_KNOWN);
	infuse_state_set(INFUSE_STATE_TIME_KNOWN);

	for (int i = 0; i < 9; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN),
			     "State cleared on iteration %d", i);
	}
}

ZTEST(application_states, test_state_timeout_update)
{
	INFUSE_STATES_ARRAY(states);

	/* Timeout should be updated on each call */
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 5);
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 10);

	for (int i = 0; i < 9; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN),
			     "State cleared on iteration %d", i);
	}

	infuse_states_snapshot(states);
	infuse_states_tick(states);
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
}

ZTEST(application_states, test_state_timeout_override)
{
	INFUSE_STATES_ARRAY(states);

	/* Calling infuse_state_set should override any existing timeout */
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 5);
	infuse_state_set(INFUSE_STATE_TIME_KNOWN);

	for (int i = 0; i < 10; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	}

	/* Calling infuse_state_set_timeout should add a timeout */
	infuse_state_set_timeout(INFUSE_STATE_TIME_KNOWN, 5);

	for (int i = 0; i < 4; i++) {
		infuse_states_snapshot(states);
		infuse_states_tick(states);
		zassert_true(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
	}

	infuse_states_snapshot(states);
	infuse_states_tick(states);
	zassert_false(infuse_state_get(INFUSE_STATE_TIME_KNOWN));
}

void test_init(void *fixture)
{
	for (int i = 0; i < UINT8_MAX; i++) {
		if (infuse_state_get(i)) {
			infuse_state_clear(i);
		}
	}
}

ZTEST_SUITE(application_states, NULL, NULL, test_init, NULL, NULL);
