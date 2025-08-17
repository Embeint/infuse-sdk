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

K_SEM_DEFINE(watchdog_expired, 0, 1);

void infuse_watchdog_warning(const struct device *dev, int channel_id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);

	/* Ignore the warning, fallthrough to actual watchdog */
}

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	k_sem_give(&watchdog_expired);
}

ZTEST(epacket_watchdog, test_watchdog)
{
#ifdef CONFIG_EPACKET_PROCESS_THREAD_SPLIT
	extern k_tid_t epacket_rx_processor_thread;
	k_tid_t thread = epacket_rx_processor_thread;
#else
	extern k_tid_t epacket_processor_thread;
	k_tid_t thread = epacket_processor_thread;
#endif /* CONFIG_EPACKET_PROCESS_THREAD_SPLIT */

	int rc;

	/* Start the watchdog */
	zassert_equal(0, infuse_watchdog_start());

	/* Watchdog shouldn't expire under normal operation */
	rc = k_sem_take(&watchdog_expired, K_SECONDS(5));
	zassert_equal(-EAGAIN, rc, "Watchdog expired prematurely");

	/* Block the processing thread */
	k_thread_suspend(thread);

	/* Suspending the processing thread should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_expired, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS + 100));
	zassert_equal(0, rc, "Watchdog did not expire");
}

void watchdog_teardown(void *fixture)
{
	/* Disable the watchdog to ensure we aren't rebooted */
	wdt_disable(INFUSE_WATCHDOG_DEV);
}

ZTEST_SUITE(epacket_watchdog, NULL, NULL, NULL, NULL, watchdog_teardown);
