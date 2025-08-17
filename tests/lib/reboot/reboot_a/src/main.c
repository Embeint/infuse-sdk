/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/time/epoch.h>
#include <infuse/drivers/watchdog.h>

static void null_dereference(void)
{
	epoch_time_set_reference(TIME_SOURCE_NONE, NULL);
	zassert_unreachable("Exception not triggered");
}

ZTEST(infuse_reboot, test_reboot)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	struct timeutil_sync_instant time_reference;
	struct infuse_reboot_state reboot_state;
	uint64_t time_2025 = epoch_time_from_gps(2347, 259218, 0);
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
		infuse_reboot(INFUSE_REBOOT_RPC, 0x1234, 0x5678);
		zassert_unreachable("infuse_reboot returned");
		break;
	case 2:
		/* Reboot state now exists */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_RPC, reboot_state.reason);
		zassert_equal(0x1234, reboot_state.info.exception_basic.program_counter);
		zassert_equal(0x5678, reboot_state.info.exception_basic.link_register);
		zassert_equal(0, reboot_state.uptime);
		zassert_equal(TIME_SOURCE_NONE, reboot_state.epoch_time_source);
		zassert_true(reboot_state.epoch_time > 0);
		/* Second call fails */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Set a valid time */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		epoch_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		/* Trigger reboot */
		infuse_reboot(INFUSE_REBOOT_HW_WATCHDOG, 4, 0);
		zassert_unreachable("infuse_reboot returned");
		break;
	case 3:
		/* Reboot state with time information */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_HW_WATCHDOG, reboot_state.reason);
		zassert_equal(4, reboot_state.info.watchdog.info1);
		zassert_equal(0, reboot_state.info.watchdog.info2);
		zassert_equal(0, reboot_state.uptime);
		zassert_equal(TIME_SOURCE_NTP, reboot_state.epoch_time_source);
		zassert_true(reboot_state.epoch_time >= time_2025);
		zassert_true(reboot_state.epoch_time < time_2025 + epoch_time_from(1, 0));
		/* Second call fails */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Sleep for a few seconds*/
		k_sleep(K_SECONDS(3));
		/* Trigger a NULL dereference */
		null_dereference();
		break;
	case 4:
		/* Reboot state with time information */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_CPU_EXCEPTION, reboot_state.reason);
		/* Uptime should be roughly correct */
		zassert_within(3, reboot_state.uptime, 1);
		/* Program counter should be somewhere in epoch_time_set_reference */
		zassert_between_inclusive(reboot_state.info.exception_basic.program_counter,
					  (uintptr_t)epoch_time_set_reference,
					  (uintptr_t)epoch_time_set_reference + 64);
		/* Link register should be somewhere in null_dereference */
		zassert_between_inclusive(reboot_state.info.exception_basic.link_register,
					  (uintptr_t)null_dereference,
					  (uintptr_t)null_dereference + 64);
		/* No time knowledge again */
		zassert_equal(TIME_SOURCE_NONE, reboot_state.epoch_time_source);
		zassert_true(reboot_state.epoch_time > 0);
		zassert_true(reboot_state.epoch_time < time_2025);
		/* Second call fails */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
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
