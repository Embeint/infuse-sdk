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

#include <infuse/drivers/watchdog.h>

K_SEM_DEFINE(watchdog_expired, 0, 1);

static uint32_t info1, info2;

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	infuse_watchdog_thread_state_lookup(channel_id, &info1, &info2);
	k_sem_give(&watchdog_expired);
}

ZTEST(drivers_watchdog, test_registration)
{
	/* Negative values */
	infuse_watchdog_thread_register(-1, _current);
	zassert_equal(-EINVAL, infuse_watchdog_thread_state_lookup(-1, &info1, &info2));

	for (int i = 0; i < 32; i++) {
		zassert_equal(-EINVAL, infuse_watchdog_thread_state_lookup(i, &info1, &info2));
		infuse_watchdog_thread_register(i, _current);
		if (i < 8) {
			zassert_equal(0, infuse_watchdog_thread_state_lookup(i, &info1, &info2));
		} else {
			zassert_equal(-EINVAL,
				      infuse_watchdog_thread_state_lookup(i, &info1, &info2));
		}
	}

	/* Currently running thread, _THREAD_QUEUED for ready queue */
	infuse_watchdog_thread_state_lookup(2, &info1, &info2);

	uint8_t channel_id = (info1 >> 0) & 0xFF;
	uint8_t thread_state = (info1 >> 8) & 0xFF;

	zassert_equal(2, channel_id, "Bad channel ID");
	zassert_equal(_THREAD_QUEUED, thread_state, "Bad thread state");
	zassert_equal(0, info2, "Bad info2");
}

static int dead_thread(void *arg1, void *arg2, void *arg3)
{
	return 0;
}
K_THREAD_DEFINE(dead, 4096, dead_thread, NULL, NULL, NULL, 0, 0, 0);

ZTEST(drivers_watchdog, test_dead_thread)
{
	infuse_watchdog_thread_register(1, dead);
	k_sleep(K_MSEC(100));

	/* Currently running thread, _THREAD_QUEUED for ready queue */
	infuse_watchdog_thread_state_lookup(1, &info1, &info2);

	uint8_t channel_id = (info1 >> 0) & 0xFF;
	uint8_t thread_state = (info1 >> 8) & 0xFF;

	zassert_equal(1, channel_id, "Bad channel ID");
	zassert_equal(_THREAD_DEAD, thread_state, "Bad thread state");
	zassert_equal(0, info2, "Bad info2");
}

ZTEST(drivers_watchdog, test_watchdog)
{
	k_timeout_t feed_period = K_NO_WAIT;
	int channel, rc;

	/* QEMU watchdog only has one timeout channel */
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(0, channel);
	zassert_false(K_TIMEOUT_EQ(K_NO_WAIT, feed_period));
	zassert_false(K_TIMEOUT_EQ(K_FOREVER, feed_period));
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(-ENOMEM, channel);
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(-ENOMEM, channel);

	/* Register watchdog against this thread */
	infuse_watchdog_thread_register(0, _current);

	/* Start watchdog */
	zassert_equal(0, infuse_watchdog_start());

	/* Second start should fail */
	zassert_equal(-EBUSY, infuse_watchdog_start());

	/* Try to allocate channel afterwards */
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(-EBUSY, channel);

	for (int i = 0; i < 5; i++) {
		infuse_watchdog_feed(0);
		k_sleep(K_SECONDS(1));
	}
	for (int i = 0; i < 3; i++) {
		infuse_watchdog_feed_all();
		k_sleep(K_SECONDS(1));
	}
	/* Failing to call task_runner_iterate should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_expired, K_NO_WAIT);
	zassert_equal(-EBUSY, rc, "Watchdog expired early");

	/* Failing to call infuse_watchdog_feed should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_expired, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS + 100));
	zassert_equal(0, rc, "Watchdog did not expire");

	/* This thread should have been in _THREAD_PENDING state */
	uint8_t channel_id = (info1 >> 0) & 0xFF;
	uint8_t thread_state = (info1 >> 8) & 0xFF;

	zassert_equal(0, channel_id, "Bad channel ID");
	zassert_equal(_THREAD_PENDING, thread_state, "Bad thread state");
	zassert_equal(&watchdog_expired.wait_q, (void *)info2, "Bad pending object");
}

void watchdog_before(void *fixture)
{
	/* Clear all registrations */
	for (int i = 0; i < 8; i++) {
		infuse_watchdog_thread_register(i, NULL);
	}
}

void watchdog_teardown(void *fixture)
{
	/* Disable the watchdog to ensure we aren't rebooted */
	wdt_disable(INFUSE_WATCHDOG_DEV);
}

ZTEST_SUITE(drivers_watchdog, NULL, NULL, watchdog_before, NULL, watchdog_teardown);
