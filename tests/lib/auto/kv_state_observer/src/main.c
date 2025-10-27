/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/states.h>
#include <infuse/time/epoch.h>

static void set_now(uint32_t gps_time)
{
	struct timeutil_sync_instant reference;
	int rc;

	reference.local = k_uptime_ticks();
	reference.ref = epoch_time_from(gps_time, 0);

	rc = epoch_time_set_reference(TIME_SOURCE_GNSS, &reference);
	zassert_equal(0, rc);
	k_sleep(K_MSEC(10));
}

ZTEST(kv_state_observer, test_led_suppress_time_unknown)
{
	struct kv_led_disable_daily_time_range time_limits = {
		.disable_start =
			{
				.hour = 2,
			},
		.disable_end =
			{
				.hour = 6,
			},
	};
	int rc;

	if (!IS_ENABLED(CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE)) {
		ztest_test_skip();
		return;
	}

	/* Write a time limit when no time is known */
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());
	rc = KV_STORE_WRITE(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE, &time_limits);
	zassert_equal(sizeof(time_limits), rc);
	k_sleep(K_MSEC(100));

	/* State should not be set */
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* Cleanup the key value */
	kv_store_delete(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE);
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
}

ZTEST(kv_state_observer, test_led_suppress)
{
	struct kv_led_disable_daily_time_range time_limits = {
		.disable_start =
			{
				.hour = 12,
				.minute = 43,
				.second = 20,
			},
		.disable_end =
			{
				.hour = 12,
				.minute = 43,
				.second = 30,
			},
	};
	int rc;

	if (!IS_ENABLED(CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE)) {
		ztest_test_skip();
		return;
	}

	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* 2024-07-02T12:43:01 UTC */
	set_now(1403959399);
	rc = KV_STORE_WRITE(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE, &time_limits);
	zassert_equal(sizeof(time_limits), rc);

	/* Time is outside the supression window (just) */
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* Naturally rolls over into suppression window */
	k_sleep(K_SECONDS(20));
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* State only stays set for a short period */
	k_sleep(K_SECONDS(9));
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(2));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* Set time to the middle of the window immediately */
	set_now(1403959399 + 25);
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(6));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* KV value updated */
	k_sleep(K_SECONDS(30));
	time_limits.disable_start.minute += 1;
	time_limits.disable_end.minute += 1;
	rc = KV_STORE_WRITE(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE, &time_limits);
	zassert_equal(sizeof(time_limits), rc);
	k_sleep(K_MSEC(10));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(17));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(2));
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* Delete KV, state immediately cleared */
	kv_store_delete(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE);
	k_sleep(K_MSEC(10));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
}

ZTEST(kv_state_observer, test_led_suppress_overflow)
{
	struct kv_led_disable_daily_time_range time_limits = {
		.disable_start =
			{
				.hour = 23,
				.minute = 59,
				.second = 45,
			},
		.disable_end =
			{
				.hour = 0,
				.minute = 0,
				.second = 15,
			},
	};
	int rc;

	if (!IS_ENABLED(CONFIG_KV_STORE_KEY_LED_DISABLE_DAILY_TIME_RANGE)) {
		ztest_test_skip();
		return;
	}

	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));

	/* 2024-07-03T23:59:01 UTC */
	set_now(1404086359);
	rc = KV_STORE_WRITE(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE, &time_limits);
	zassert_equal(sizeof(time_limits), rc);

	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(43));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(2));
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(28));
	zassert_true(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
	k_sleep(K_SECONDS(4));
	zassert_false(infuse_state_get(INFUSE_STATE_LED_SUPPRESS));
}

ZTEST(kv_state_observer, test_application_active)
{
	struct kv_application_active active;
	int rc;

	if (!IS_ENABLED(CONFIG_KV_STORE_KEY_APPLICATION_ACTIVE)) {
		/* State should be automatically enabled if KEY is not enabled */
		zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));
		return;
	}

	/* Enabled while not present (fail open) */
	zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Write inactive */
	active.active = 0x00;
	rc = KV_STORE_WRITE(KV_KEY_APPLICATION_ACTIVE, &active);
	zassert_equal(sizeof(active), rc);
	zassert_false(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Delete while inactive */
	rc = kv_store_delete(KV_KEY_APPLICATION_ACTIVE);
	zassert_equal(0, rc);
	zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Write active */
	active.active = 0x01;
	rc = KV_STORE_WRITE(KV_KEY_APPLICATION_ACTIVE, &active);
	zassert_equal(sizeof(active), rc);
	zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Write inactive */
	active.active = 0x00;
	rc = KV_STORE_WRITE(KV_KEY_APPLICATION_ACTIVE, &active);
	zassert_equal(sizeof(active), rc);
	zassert_false(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Write different active */
	active.active = 0xA9;
	rc = KV_STORE_WRITE(KV_KEY_APPLICATION_ACTIVE, &active);
	zassert_equal(sizeof(active), rc);
	zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));

	/* Delete while active */
	rc = kv_store_delete(KV_KEY_APPLICATION_ACTIVE);
	zassert_equal(0, rc);
	zassert_true(infuse_state_get(INFUSE_STATE_APPLICATION_ACTIVE));
}

void test_init(void *fixture)
{
	struct timeutil_sync_instant reference = {
		.local = k_uptime_ticks(),
		.ref = 1,
	};

	kv_store_delete(KV_KEY_LED_DISABLE_DAILY_TIME_RANGE);
	kv_store_delete(KV_KEY_APPLICATION_ACTIVE);
	epoch_time_set_reference(TIME_SOURCE_NONE, &reference);
}

ZTEST_SUITE(kv_state_observer, NULL, NULL, test_init, NULL, NULL);
