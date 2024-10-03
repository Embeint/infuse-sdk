/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include <infuse/drivers/watchdog.h>

#define INFUSE_WATCHDOG_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))
#define MAX_CHANNELS        8

static k_tid_t threads[MAX_CHANNELS];

LOG_MODULE_REGISTER(wdog, LOG_LEVEL_INF);

int infuse_watchdog_install(k_timeout_t *feed_period)
{
	const struct wdt_timeout_cfg timeout_cfg = INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG;
	int wdog_channel = wdt_install_timeout(INFUSE_WATCHDOG_DEV, &timeout_cfg);

	if (wdog_channel < 0) {
		if (wdog_channel == -EBUSY) {
			LOG_ERR("Attempted to allocate wdog channel after wdog started");
		} else if (wdog_channel == -ENOMEM) {
			LOG_ERR("Insufficient wdog channels");
		}
		*feed_period = K_FOREVER;
	} else {
		*feed_period = INFUSE_WATCHDOG_FEED_PERIOD;
	}
	return wdog_channel;
}

int infuse_watchdog_start(void)
{
	int rc;

	rc = wdt_setup(INFUSE_WATCHDOG_DEV, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (rc < 0) {
		LOG_ERR("Watchdog failed to start (%d)", rc);
	}
	return rc;
}

void infuse_watchdog_feed(int wdog_channel)
{
	/* Feed the watchdog */
	if (wdog_channel >= 0) {
		(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);
	}
}

void infuse_watchdog_thread_register(int wdog_channel, k_tid_t thread)
{
	if (wdog_channel < 0) {
		return;
	}
	(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);
	if (wdog_channel >= MAX_CHANNELS) {
		return;
	}
	threads[wdog_channel] = thread;
}

int infuse_watchdog_thread_state_lookup(int wdog_channel, uint32_t *info1, uint32_t *info2)
{
	uint32_t common_state;
	k_tid_t thread;

	if (wdog_channel < 0) {
		return -EINVAL;
	}
	if (wdog_channel >= MAX_CHANNELS) {
		return -EINVAL;
	}
	if (threads[wdog_channel] == NULL) {
		return -EINVAL;
	}
	thread = threads[wdog_channel];
	common_state = thread->base.thread_state & 0xFF;

	*info1 = (wdog_channel & 0xFF) | (common_state << 8);
	if (thread->base.thread_state & _THREAD_PENDING) {
		*info2 = (uintptr_t)thread->base.pended_on;
	} else {
		*info2 = 0x00;
	}
	return 0;
}

void infuse_watchdog_feed_all(void)
{
	for (int i = 0; i < 8; i++) {
		(void)wdt_feed(INFUSE_WATCHDOG_DEV, i);
	}
}
