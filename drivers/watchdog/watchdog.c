/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>

#define INFUSE_WATCHDOG_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))

static k_tid_t threads[8];

void infuse_watchdog_thread_register(int wdog_channel, k_tid_t thread)
{
	if (wdog_channel < 0) {
		return;
	}
	(void)wdt_feed(INFUSE_WATCHDOG_DEV, wdog_channel);
	if (wdog_channel >= ARRAY_SIZE(threads)) {
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
	if (wdog_channel >= ARRAY_SIZE(threads)) {
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
