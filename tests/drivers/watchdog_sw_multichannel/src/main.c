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

ZTEST(drivers_watchdog, test_watchdog_num_channels)
{
	k_timeout_t feed_period;
	int channel;

	/* Up to 8 channels can be registered */
	for (int i = 0; i < 8; i++) {
		channel = infuse_watchdog_install(&feed_period);
		zassert_equal(i, channel);
	}
	channel = infuse_watchdog_install(&feed_period);
	zassert_equal(-ENOMEM, channel);
}

ZTEST(drivers_watchdog, test_watchdog)
{
	k_timeout_t feed_period;
	int channels[4];
	int rc;

	/* Multiple emulated channels */
	for (int i = 0; i < 4; i++) {
		feed_period = K_NO_WAIT;
		channels[i] = infuse_watchdog_install(&feed_period);
		zassert_true(channels[i] >= 0);
		zassert_false(K_TIMEOUT_EQ(K_NO_WAIT, feed_period));
		zassert_false(K_TIMEOUT_EQ(K_FOREVER, feed_period));

		/* Register watchdog against this thread */
		infuse_watchdog_thread_register(channels[i], _current);
	}

	/* Feed watchdog early, no adverse affects */
	for (int i = 0; i < 4; i++) {
		infuse_watchdog_feed(channels[i]);
	}
	zassert_equal(-EAGAIN,
		      k_sem_take(&watchdog_warning, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS)));
	zassert_equal(-EBUSY, k_sem_take(&watchdog_expired, K_NO_WAIT));

	/* Start watchdog */
	zassert_equal(0, infuse_watchdog_start());

	/* Starting again should fail but not affect test */
	zassert_equal(-EBUSY, infuse_watchdog_start());

	/* Feeding invalid channel should but not affect test */
	infuse_watchdog_feed(-1);

	/* Feed for a while */
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) {
			infuse_watchdog_feed(channels[j]);
		}
		k_sleep(K_SECONDS(1));
	}
	/* Registering should also feed */
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) {
			infuse_watchdog_thread_register(channels[j], _current);
		}
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

	/* Feed 3 of the 4 channels, 1 second after feeding all the channels */
	infuse_watchdog_feed_all();
	k_sleep(K_SECONDS(1));
	for (int i = 0; i < 4; i++) {
		if (i == 2) {
			continue;
		}
		infuse_watchdog_feed(channels[i]);
	}

	/* Failing to call infuse_watchdog_feed on the missed channel should result in a timeout */
	rc = k_sem_take(&watchdog_warning, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS - 1000));
	zassert_equal(0, rc, "Watchdog warning didn't fire");
	rc = k_sem_take(&watchdog_expired, K_MSEC(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS + 10));
	zassert_equal(0, rc, "Watchdog did not expire");

	/* Warning should have been received CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS early */
	zassert_within(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS, warning, 10,
		       "Watchdog warning not at expected time");
	zassert_equal(channels[2], expired_channel, "Unexpected channel ID");
}

void watchdog_before(void *fixture)
{
	void infuse_watchdog_test_reset(void);

	/* Clear all registrations */
	for (int i = 0; i < 8; i++) {
		infuse_watchdog_thread_register(i, NULL);
	}
	infuse_watchdog_test_reset();
}

void watchdog_teardown(void *fixture)
{
	/* Disable the watchdog to ensure we aren't rebooted */
	wdt_disable(INFUSE_WATCHDOG_DEV);
}

ZTEST_SUITE(drivers_watchdog, NULL, NULL, watchdog_before, NULL, watchdog_teardown);
