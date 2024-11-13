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
#include <zephyr/sys/__assert.h>

#include <infuse/drivers/watchdog.h>

#define INFUSE_WATCHDOG_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))
#define SOFTWARE_WARNING_MS                                                                        \
	(CONFIG_INFUSE_WATCHDOG_PERIOD_MS - CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS)
#define MAX_CHANNELS 8

static k_tid_t threads[MAX_CHANNELS];

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
static void software_watchdog_alarm(struct k_timer *timer);

static K_TIMER_DEFINE(watchdog_warning_timer, software_watchdog_alarm, NULL);
static int64_t channel_expires[MAX_CHANNELS];
static int channel_max;
static int8_t wdog_running;

BUILD_ASSERT(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS < CONFIG_INFUSE_WATCHDOG_FEED_EARLY_MS,
	     "Software alarm will fire before feed timeout");
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */

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
#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
	channel_max = MAX(channel_max, wdog_channel);
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
	return wdog_channel;
}

int infuse_watchdog_start(void)
{
	int rc;

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
	int64_t ms_now = k_uptime_get();

	for (int i = 0; i <= channel_max; i++) {
		channel_expires[i] = ms_now + SOFTWARE_WARNING_MS;
	}
	k_timer_start(&watchdog_warning_timer, K_TIMEOUT_ABS_MS(channel_expires[0]), K_FOREVER);
	wdog_running = true;
	LOG_DBG("Software timer starting (expiry @ %lld ms)", channel_expires[0]);
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */

	rc = wdt_setup(INFUSE_WATCHDOG_DEV, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (rc < 0) {
		LOG_ERR("Watchdog failed to start (%d)", rc);
	}
	return rc;
}

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
static void infuse_watchdog_software_feed(int wdog_channel)
{
	/* Update software warning */
	int64_t ms_expire = INT64_MAX;

	if (!wdog_running) {
		return;
	}

	/* Update expiry for this channel */
	channel_expires[wdog_channel] = k_uptime_get() + SOFTWARE_WARNING_MS;
	/* Get new global expiry */
	for (int i = 0; i <= channel_max; i++) {
		ms_expire = MIN(ms_expire, channel_expires[i]);
	}
	/* Set new timeout */
	k_timer_start(&watchdog_warning_timer, K_TIMEOUT_ABS_MS(ms_expire), K_FOREVER);
	LOG_DBG("Software timer now expires @ %lldms)", ms_expire);
}
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */

void infuse_watchdog_feed(int wdog_channel)
{
	if (wdog_channel < 0) {
		return;
	}

	/* Feed the watchdog */
	(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
	infuse_watchdog_software_feed(wdog_channel);
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
}

void infuse_watchdog_thread_register(int wdog_channel, k_tid_t thread)
{
	if (wdog_channel < 0) {
		return;
	}
	infuse_watchdog_feed(wdog_channel);
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

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
static void software_watchdog_alarm(struct k_timer *timer)
{
	int64_t ms_now = k_uptime_get();
	uint8_t channel = UINT8_MAX;

	/* Find the channel that expired */
	for (int i = 0; i <= channel_max; i++) {
		if (channel_expires[i] <= ms_now) {
			channel = i;
			break;
		}
	}
	__ASSERT(channel != UINT8_MAX, "No channel expired?");

	LOG_WRN("Software warning on channel %d", channel);
	infuse_watchdog_warning(INFUSE_WATCHDOG_DEV, channel);
}
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
