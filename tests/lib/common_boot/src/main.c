/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/ztest.h>

#include <infuse/fs/kv_store.h>
#include <infuse/fs/kv_types.h>
#include <infuse/reboot.h>
#include <infuse/common_boot.h>
#include <infuse/time/epoch.h>

#define KV_FINAL_RESET_KEY 0x12349876

static void null_dereference(void)
{
	epoch_time_set_reference(TIME_SOURCE_NONE, NULL);
}

static __noinit int resetting_with_bad_id;

ZTEST(common_boot, test_boot)
{
	KV_STRING_CONST(sim_uicc, "89000000000012345");
	KV_KEY_TYPE(KV_KEY_LTE_SIM_IMSI) sim_imsi;
	KV_KEY_TYPE(KV_KEY_INFUSE_APPLICATION_ID) id;
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	uint64_t time_2020 = epoch_time_from_gps(2086, 259218, 0);
	uint64_t time_2025 = epoch_time_from_gps(2347, 259218, 0);
	struct timeutil_sync_instant time_reference;
	struct infuse_reboot_state reboot_state;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count and app ID */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);
	rc = KV_STORE_READ(KV_KEY_INFUSE_APPLICATION_ID, &id);
	zassert_equal(sizeof(id), rc);
	zassert_equal(CONFIG_INFUSE_APPLICATION_ID, id.application_id);

	if (resetting_with_bad_id == KV_FINAL_RESET_KEY) {
		/* KV store should have been reset */
		zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_UICC), "KV store not reset");
		zassert_false(kv_store_key_exists(KV_KEY_LTE_SIM_IMSI), "KV store not reset");
		zassert_equal(1, reboots.count, "KV store not reset");
		/* We should still have the reboot reason state */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_EXTERNAL_TRIGGER, reboot_state.reason);
		zassert_equal(0x56, reboot_state.info.generic.info1);
		zassert_equal(0x78, reboot_state.info.generic.info2);
		/* Test complete */
		return;
	}

	switch (reboots.count) {
	case 1:
		sim_imsi.imsi = 123456789012345ll;
		/* Set SIM values */
		zassert_equal(sizeof(sim_uicc), KV_STORE_WRITE(KV_KEY_LTE_SIM_UICC, &sim_uicc));
		zassert_equal(sizeof(sim_imsi), KV_STORE_WRITE(KV_KEY_LTE_SIM_IMSI, &sim_imsi));
		/* No reboot information yet */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(-ENOENT, rc);
		zassert_equal(INFUSE_REBOOT_UNKNOWN, reboot_state.reason);
		/* Should have no time source */
		zassert_equal(TIME_SOURCE_NONE, epoch_time_get_source());
		/* Trigger reboot */
		infuse_reboot(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0x12, 0x34);
		zassert_unreachable("Failed to reboot");
		break;
	case 2:
		/* Reboot information */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		zassert_equal(INFUSE_REBOOT_EXTERNAL_TRIGGER, reboot_state.reason);
		zassert_equal(0x12, reboot_state.info.generic.info1);
		zassert_equal(0x34, reboot_state.info.generic.info2);
		/* Time should have been restored */
		zassert_equal(TIME_SOURCE_RECOVERED | TIME_SOURCE_NONE, epoch_time_get_source());
		zassert_true(epoch_time_now() > time_2020);
		zassert_true(epoch_time_now() < time_2020 + epoch_time_from(1, 0));
		/* Querying data again should succeed */
		rc = infuse_common_boot_last_reboot(&reboot_state);
		zassert_equal(0, rc);
		/* Set a good time */
		time_reference.local = k_uptime_ticks();
		time_reference.ref = time_2025;
		rc = epoch_time_set_reference(TIME_SOURCE_NTP, &time_reference);
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
		zassert_equal(TIME_SOURCE_RECOVERED | TIME_SOURCE_NTP, epoch_time_get_source());
		zassert_true(epoch_time_now() > time_2025);
		zassert_true(epoch_time_now() < time_2025 + epoch_time_from(1, 0));

		/* Write some random value to KV_KEY_LTE_SIM_UICC to validate it is erased */
		KV_STRING_CONST(sim_uicc, "UICC_TEST");

		rc = KV_STORE_WRITE(KV_KEY_LTE_SIM_UICC, &sim_uicc);
		zassert_equal(sizeof(sim_uicc), rc);

		/* Reset one final time after writing the KV application ID to a bad value */
		id.application_id = CONFIG_INFUSE_APPLICATION_ID + 1;
		rc = KV_STORE_WRITE(KV_KEY_INFUSE_APPLICATION_ID, &id);
		zassert_equal(sizeof(id), rc);
		resetting_with_bad_id = KV_FINAL_RESET_KEY;
		infuse_reboot(INFUSE_REBOOT_EXTERNAL_TRIGGER, 0x56, 0x78);
		zassert_unreachable("Failed to reboot");
		break;
	default:
		zassert_unreachable("Unexpected reboot count");
	}
}

ZTEST_SUITE(common_boot, NULL, NULL, NULL, NULL, NULL);
