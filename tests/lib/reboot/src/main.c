/**
 * @file
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/ztest.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/time/civil.h>

static void null_dereference(void)
{
	civil_time_set_reference(TIME_SOURCE_NONE, NULL);
	zassert_unreachable("Exception not triggered");
}

ZTEST(infuse_reboot, test_reboot)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	uintptr_t state_addr = DT_REG_ADDR(DT_GPARENT(DT_CHOSEN(infuse_reboot_state)));
	struct timeutil_sync_instant time_reference;
	struct infuse_reboot_state reboot_state;
	uint64_t time_2025 = civil_time_from_gps(2347, 259218, 0);
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
		zassert_equal(0x1234, reboot_state.param_1.program_counter);
		zassert_equal(0x5678, reboot_state.param_2.link_register);
		zassert_equal(0, reboot_state.uptime);
		zassert_equal(TIME_SOURCE_NONE, reboot_state.civil_time_source);
		zassert_true(reboot_state.civil_time > 0);
		/* Second call fails */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Set a valid time */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		civil_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		/* Trigger reboot */
		infuse_reboot(INFUSE_REBOOT_WATCHDOG, 4, 0);
		zassert_unreachable("infuse_reboot returned");
		break;
	case 3:
		/* Reboot state with time information */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_WATCHDOG, reboot_state.reason);
		zassert_equal(4, reboot_state.param_1.watchdog_channel);
		zassert_equal(0, reboot_state.param_2.link_register);
		zassert_equal(0, reboot_state.uptime);
		zassert_equal(TIME_SOURCE_NTP, reboot_state.civil_time_source);
		zassert_true(reboot_state.civil_time >= time_2025);
		zassert_true(reboot_state.civil_time < time_2025 + civil_time_from(1, 0));
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
		/* Program counter should be somewhere in civil_time_set_reference */
		zassert_between_inclusive(reboot_state.param_1.program_counter, (uintptr_t)civil_time_set_reference,
					  (uintptr_t)civil_time_set_reference + 64);
		/* Link register should be somewhere in null_dereference */
		zassert_between_inclusive(reboot_state.param_2.link_register, (uintptr_t)null_dereference,
					  (uintptr_t)null_dereference + 64);
		/* No time knowledge again */
		zassert_equal(TIME_SOURCE_NONE, reboot_state.civil_time_source);
		zassert_true(reboot_state.civil_time > 0);
		zassert_true(reboot_state.civil_time < time_2025);
		/* Second call fails */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Reboot again */
		infuse_reboot(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0, 0);
		zassert_unreachable("infuse_reboot returned");
		break;
	case 5:
		/* Corrupt a random byte in the retained memory */
		((uint8_t *)state_addr)[3] += 2;
		/* Querying info should fail */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(-ENOENT, rc);
		/* Schedule a delayed reboot */
		infuse_reboot_delayed(INFUSE_REBOOT_EXTERNAL_TRIGGER, 1000, 2000, K_SECONDS(3));
		zassert_equal(0, k_sleep(K_MSEC(2500)));
		/* Set the time reference just before the reboot */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		civil_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		/* Sleep again */
		k_sleep(K_SECONDS(1));
		zassert_unreachable("Unexpected reboot count");
	case 6:
		/* Reboot state */
		rc = infuse_reboot_state_query(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_EXTERNAL_TRIGGER, reboot_state.reason);
		zassert_equal(1000, reboot_state.param_1.watchdog_channel);
		zassert_equal(2000, reboot_state.param_2.link_register);
		/* Uptime should have been updated at point of reboot */
		zassert_true(reboot_state.uptime >= 3);
		/* Time reference should be valid and about half a second after the reference */
		zassert_equal(TIME_SOURCE_NTP, reboot_state.civil_time_source);
		zassert_within(reboot_state.civil_time, time_2025 + INFUSE_CIVIL_TIME_TICKS_PER_SEC / 2,
			       INFUSE_CIVIL_TIME_TICKS_PER_SEC / 10);
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

	/* Initialise KV store */
	rc = kv_store_init();

	/* Get current reboot count */
	if (rc == 0) {
		rc = kv_store_read_fallback(KV_KEY_REBOOTS, &reboot, sizeof(reboot), &reboot_fallback,
					    sizeof(reboot_fallback));
		if (rc == sizeof(reboot)) {
			/* Increment reboot counter */
			reboot.count += 1;
			(void)KV_STORE_WRITE(KV_KEY_REBOOTS, &reboot);
		}
	}
	return NULL;
}

ZTEST_SUITE(infuse_reboot, NULL, test_init, NULL, NULL, NULL);
