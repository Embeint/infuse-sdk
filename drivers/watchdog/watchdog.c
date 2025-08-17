/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
static int channel_max = -1;
static int8_t wdog_running;

#ifdef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
static uint32_t sw_channels_mask;
static uint32_t sw_channels_fed;
static int global_channel;
#endif

BUILD_ASSERT(CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING_MS < CONFIG_INFUSE_WATCHDOG_FEED_EARLY_MS,
	     "Software alarm will fire before feed timeout");
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */

LOG_MODULE_REGISTER(wdog, LOG_LEVEL_INF);

#ifdef CONFIG_ZTEST

void infuse_watchdog_test_reset(void)
{
#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
	k_timer_stop(&watchdog_warning_timer);
	memset(channel_expires, 0x00, sizeof(channel_expires));
	channel_max = -1;
	wdog_running = false;
#ifdef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
#endif /* CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL */
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
}

#endif /* CONFIG_ZTEST */

int infuse_watchdog_install(k_timeout_t *feed_period)
{
	int wdog_channel;

#ifdef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
	if (channel_max == (MAX_CHANNELS - 1)) {
		LOG_ERR("Insufficient wdog channels");
		wdog_channel = -ENOMEM;
		*feed_period = K_FOREVER;
	} else {
		wdog_channel = channel_max + 1;
		sw_channels_mask |= 1 << wdog_channel;
		*feed_period = INFUSE_WATCHDOG_FEED_PERIOD;
	}
#else
	const struct wdt_timeout_cfg timeout_cfg = INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG;

	wdog_channel = wdt_install_timeout(INFUSE_WATCHDOG_DEV, &timeout_cfg);
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
#endif /* CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL */

#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
	channel_max = MAX(channel_max, wdog_channel);
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
	return wdog_channel;
}

int infuse_watchdog_start(void)
{
	int rc;

#ifdef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
	const struct wdt_timeout_cfg timeout_cfg = INFUSE_WATCHDOG_DEFAULT_TIMEOUT_CFG;

	global_channel = wdt_install_timeout(INFUSE_WATCHDOG_DEV, &timeout_cfg);
	if (global_channel < 0) {
		LOG_ERR("Watchdog failed to configure global channel (%d)", global_channel);
		return global_channel;
	}
#endif /* CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL */

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

#ifdef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
	/* Feed the hardware watchdog when all software channels have been fed */
	sw_channels_fed |= (1 << wdog_channel);
	LOG_DBG("Fed channels: %02X %02X", sw_channels_fed, sw_channels_mask);
	if (sw_channels_fed == sw_channels_mask) {
		(void)wdt_feed(INFUSE_WATCHDOG_DEV, global_channel);
		sw_channels_fed = 0;
	}
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */

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

#ifndef CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL
	/* Feed the watchdog */
	(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);
#endif /* CONFIG_INFUSE_WATCHDOG_SW_MULTICHANNEL */

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
	for (int i = 0; i < MAX_CHANNELS; i++) {
		(void)wdt_feed(INFUSE_WATCHDOG_DEV, i);
#ifdef CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING
		infuse_watchdog_software_feed(i);
#endif /* CONFIG_INFUSE_WATCHDOG_SOFTWARE_WARNING */
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
