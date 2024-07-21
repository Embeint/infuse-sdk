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
#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>

static void null_dereference(void)
{
	civil_time_set_reference(TIME_SOURCE_NONE, NULL);
}

ZTEST(common_boot, test_boot)
{
	KV_STRING_CONST(sim_uicc, "89000000000012345");
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	uint64_t time_2020 = civil_time_from_gps(2086, 259218, 0);
	uint64_t time_2025 = civil_time_from_gps(2347, 259218, 0);
	struct timeutil_sync_instant time_reference;
	struct infuse_reboot_state reboot_state;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);

	switch (reboots.count) {
	case 1:
		/* Set SIM value */
		zassert_equal(sizeof(sim_uicc), KV_STORE_WRITE(KV_KEY_LTE_SIM_UICC, &sim_uicc));
		/* No reboot information yet */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(-ENOENT, rc);
		zassert_equal(INFUSE_REBOOT_UNKNOWN, reboot_state.reason);
		/* Should have no time source */
		zassert_equal(TIME_SOURCE_NONE, civil_time_get_source());
		/* Trigger reboot */
		infuse_reboot(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0x12, 0x34);
		zassert_unreachable("Failed to reboot");
		break;
	case 2:
		/* Reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_EXTERNAL_TRIGGER, reboot_state.reason);
		zassert_equal(0x12, reboot_state.param_1.program_counter);
		zassert_equal(0x34, reboot_state.param_2.link_register);
		/* Time should have been restored */
		zassert_equal(TIME_SOURCE_RECOVERED | TIME_SOURCE_NONE, civil_time_get_source());
		zassert_true(civil_time_now() > time_2020);
		zassert_true(civil_time_now() < time_2020 + civil_time_from(1, 0));
		/* Querying data again should succeed */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		/* Set a good time */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		rc = civil_time_set_reference(TIME_SOURCE_NTP, &time_reference);
		zassert_equal(0, rc);
		/* Reboot through a crash */
		null_dereference();
		zassert_unreachable("Failed to crash");
	case 3:
		/* Reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal((enum infuse_reboot_reason)K_ERR_CPU_EXCEPTION, reboot_state.reason);
		/* Time should have been restored */
		zassert_equal(TIME_SOURCE_RECOVERED | TIME_SOURCE_NTP, civil_time_get_source());
		zassert_true(civil_time_now() > time_2025);
		zassert_true(civil_time_now() < time_2025 + civil_time_from(1, 0));
		/* Test complete */
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
	}
}

ZTEST_SUITE(common_boot, NULL, NULL, NULL, NULL, NULL);
