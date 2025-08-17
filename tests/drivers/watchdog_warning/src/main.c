/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/drivers/watchdog.h>

static uint8_t expired_channel = UINT8_MAX;
static int64_t warning;
K_SEM_DEFINE(watchdog_warning, 0, 1);
K_SEM_DEFINE(watchdog_expired, 0, 1);

void infuse_watchdog_warning(const struct device *dev, int channel_id)
{
	zassert_equal(INFUSE_WATCHDOG_DEV, dev);
	expired_channel = channel_id;
	warning = k_uptime_get();
	k_sem_give(&watchdog_warning);
}

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	zassert_equal(INFUSE_WATCHDOG_DEV, dev);
	warning = k_uptime_get() - warning;
	k_sem_give(&watchdog_expired);
}

ZTEST(drivers_watchdog, test_watchdog)
{
	k_timeout_t feed_period;
	int extra_ms = IS_ENABLED(CONFIG_COVERAGE) ? 50 : 0;
	int channel, rc;

	/* QEMU watchdog only has one timeout channel */
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(0, channel);

	/* Register watchdog against this thread */
	infuse_watchdog_thread_register(0, _current);

	/* Feed watchdog early, no adverse affects */
	infuse_watchdog_feed(0);
	zassert_equal(-EAGAIN,
		      k_sem_take(&watchdog_warning, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS)));
	zassert_equal(-EBUSY, k_sem_take(&watchdog_expired, K_NO_WAIT));

	/* Start watchdog */
	zassert_equal(0, infuse_watchdog_start());

	/* Feed for a while */
	for (int i = 0; i < 3; i++) {
		infuse_watchdog_feed(0);
		k_sleep(K_SECONDS(1));
	}
	/* Registering should also feed */
	for (int i = 0; i < 3; i++) {
		infuse_watchdog_thread_register(0, _current);
		k_sleep(K_SECONDS(1));
	}
	/* infuse_watchdog_feed_all should also feed */
	for (int i = 0; i < 3; i++) {
		infuse_watchdog_feed_all();
		k_sleep(K_SECONDS(1));
	}

	/* Nothing should have expired */
	zassert_equal(-EBUSY, k_sem_take(&watchdog_warning, K_NO_WAIT), "Warning fired early");
	zassert_equal(-EBUSY, k_sem_take(&watchdog_expired, K_NO_WAIT), "Watchdog expired early");

	/* Failing to call infuse_watchdog_feed should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_warning, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS));
	zassert_equal(0, rc, "Watchdog warning didn't fire");
	rc = k_sem_take(&watchdog_expired,
			K_MSEC(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS + 10 + extra_ms));
	zassert_equal(0, rc, "Watchdog did not expire");

	/* Warning should have been received CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS early */
	zassert_within(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS, warning, 5 + extra_ms,
		       "Watchdog warning not at expected time");
	zassert_equal(0, expired_channel, "Unexpected channel ID");
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
