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

ZTEST(common_boot, test_boot)
{
	KV_KEY_TYPE(KV_KEY_REBOOTS) reboots;
	ssize_t rc;

	/* KV store should have been initialised and populated with a reboot count */
	rc = KV_STORE_READ(KV_KEY_REBOOTS, &reboots);
	zassert_equal(sizeof(reboots), rc);
}

ZTEST_SUITE(common_boot, NULL, NULL, NULL, NULL, NULL);
