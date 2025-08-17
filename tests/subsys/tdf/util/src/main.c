/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/tdf/tdf.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>

ZTEST(tdf_util, test_acc_range_to_tdf)
{
	zassert_equal(TDF_ACC_2G, tdf_id_from_accelerometer_range(2));
	zassert_equal(TDF_ACC_4G, tdf_id_from_accelerometer_range(4));
	zassert_equal(TDF_ACC_8G, tdf_id_from_accelerometer_range(8));
	zassert_equal(TDF_ACC_16G, tdf_id_from_accelerometer_range(16));
}

ZTEST(tdf_util, test_gyro_range_to_tdf)
{
	zassert_equal(TDF_GYR_125DPS, tdf_id_from_gyroscope_range(125));
	zassert_equal(TDF_GYR_250DPS, tdf_id_from_gyroscope_range(250));
	zassert_equal(TDF_GYR_500DPS, tdf_id_from_gyroscope_range(500));
	zassert_equal(TDF_GYR_1000DPS, tdf_id_from_gyroscope_range(1000));
	zassert_equal(TDF_GYR_2000DPS, tdf_id_from_gyroscope_range(2000));
}

ZTEST(tdf_util, test_bt_addr_conv)
{
	const bt_addr_le_t addr_pub = {.type = BT_ADDR_LE_PUBLIC, .a = {{0, 1, 2, 3, 4, 5}}};
	const bt_addr_le_t addr_rnd = {.type = BT_ADDR_LE_RANDOM, .a = {{4, 5, 6, 7, 8, 9}}};
	struct tdf_struct_bt_addr_le tdf_addr;

	tdf_bt_addr_le_from_stack(&addr_pub, &tdf_addr);
	zassert_equal(BT_ADDR_LE_PUBLIC, tdf_addr.type);
	zassert_mem_equal(tdf_addr.val, addr_pub.a.val, 6);

	tdf_bt_addr_le_from_stack(&addr_rnd, &tdf_addr);
	zassert_equal(BT_ADDR_LE_RANDOM, tdf_addr.type);
	zassert_mem_equal(tdf_addr.val, addr_rnd.a.val, 6);
}

ZTEST_SUITE(tdf_util, NULL, NULL, NULL, NULL, NULL);
