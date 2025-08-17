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
#include <infuse/task_runner/runner.h>

K_SEM_DEFINE(watchdog_expired, 0, 1);

void infuse_watchdog_expired(const struct device *dev, int channel_id)
{
	k_sem_give(&watchdog_expired);
}

ZTEST(data_logger_watchdog, test_watchdog)
{
	extern const k_tid_t logger_commit_thread;
	int rc;

	/* Start the watchdog */
	zassert_equal(0, infuse_watchdog_start());

	/* Watchdog shouldn't expire under normal operation */
	rc = k_sem_take(&watchdog_expired, K_SECONDS(5));
	zassert_equal(-EAGAIN, rc, "Watchdog expired prematurely");

	/* Block the RPC server thread */
	k_thread_suspend(logger_commit_thread);

	/* Failing to call task_runner_iterate should result in a watchdog interrupt */
	rc = k_sem_take(&watchdog_expired, K_MSEC(CONFIG_INFUSE_WATCHDOG_PERIOD_MS + 100));
	zassert_equal(0, rc, "Watchdog did not expire");
}

void watchdog_teardown(void *fixture)
{
	/* Disable the watchdog to ensure we aren't rebooted */
	wdt_disable(INFUSE_WATCHDOG_DEV);
}

ZTEST_SUITE(data_logger_watchdog, NULL, NULL, NULL, NULL, watchdog_teardown);
