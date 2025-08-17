/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/states.h>
#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/time/epoch.h>
#include <infuse/drivers/watchdog.h>

ZTEST(infuse_reboot, test_reboot)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	uintptr_t state_addr = DT_REG_ADDR(DT_GPARENT(DT_CHOSEN(infuse_reboot_state)));
	struct timeutil_sync_instant time_reference;
	struct infuse_reboot_state reboot_state;
	uint64_t time_2025 = epoch_time_from_gps(2347, 259218, 0);
	k_timeout_t feed_period;
	int wdog_channel;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		/* No initial reboot state */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Trigger reboot */
		infuse_reboot(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0, 0);
		zassert_unreachable("infuse_reboot returned");
		break;
	case 2:
		/* Corrupt a random byte in the retained memory */
		((uint8_t *)state_addr)[3] += 2;
		/* Querying info should fail */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		zassert_false(infuse_state_get(INFUSE_STATE_REBOOTING));
		/* Schedule a delayed reboot, check state was set */
		infuse_reboot_delayed(INFUSE_REBOOT_EXTERNAL_TRIGGER, 1000, 2000, K_SECONDS(3));
		zassert_true(infuse_state_get(INFUSE_STATE_REBOOTING));
		/* Set the time reference just before the reboot */
		zassert_equal(0, k_sleep(K_MSEC(2500)));
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		epoch_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		/* Sleep again */
		k_sleep(K_SECONDS(1));
		zassert_unreachable("Unexpected reboot count");
	case 3:
		/* Reboot state */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_EXTERNAL_TRIGGER, reboot_state.reason);
		zassert_equal(1000, reboot_state.info.generic.info1);
		zassert_equal(2000, reboot_state.info.generic.info2);
		/* Uptime should have been updated at point of reboot */
		zassert_true(reboot_state.uptime >= 3);
		/* Time reference should be valid and about half a second after the reference */
		zassert_equal(TIME_SOURCE_NTP, reboot_state.epoch_time_source);
		zassert_within(reboot_state.epoch_time,
			       time_2025 + INFUSE_EPOCH_TIME_TICKS_PER_SEC / 2,
			       INFUSE_EPOCH_TIME_TICKS_PER_SEC / 10);
		/* Set the time reference 2 seconds before the reboot */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		epoch_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		/* Reboot through watchdog timeout */
		wdog_channel = infuse_watchdog_install(&feed_period);
		zassert_equal(0, wdog_channel);
		zassert_equal(0, infuse_watchdog_start());
		infuse_watchdog_feed(0);
		k_sleep(feed_period);
		k_sleep(feed_period);
		zassert_unreachable("Watchdog failed to reboot");
	case 4:
		/* Reboot information */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_HW_WATCHDOG, reboot_state.reason);
		/* Time reference should be valid and about 2 seconds after the reference */
		zassert_equal(TIME_SOURCE_NTP, reboot_state.epoch_time_source);
		zassert_within(reboot_state.epoch_time,
			       time_2025 + 2 * INFUSE_EPOCH_TIME_TICKS_PER_SEC,
			       INFUSE_EPOCH_TIME_TICKS_PER_SEC);
		/* Test sequence complete */
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
		break;
	}
}

void *test_init(void)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboot_fallback = {0}, reboot = {0};
	int rc;

	/* Get current reboot count */
	rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboot, sizeof(reboot), &reboot_fallback,
				    sizeof(reboot_fallback));
	if (rc == sizeof(reboot)) {
		/* Increment reboot counter */
		reboot.count += 1;
		(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboot);
	}
	return NULL;
}

ZTEST_SUITE(infuse_reboot, NULL, test_init, NULL, NULL, NULL);
