/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <infuse/tdf/tdf.h>
#include <infuse/tdf/util.h>
#include <infuse/time/epoch.h>
#include <infuse/reboot.h>

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

ZTEST(tdf_util, test_reboot_info)
{
	struct infuse_reboot_state state;
	struct tdf_reboot_info tdf;

	/* Generic storage */
	state.reason = INFUSE_REBOOT_MCUMGR;
	state.uptime = 12345678;
	state.hardware_reason = 0x9876;
	state.info_type = INFUSE_REBOOT_INFO_GENERIC;
	state.info.generic.info1 = 0x1234;
	state.info.generic.info2 = 0x2345;
	tdf_reboot_info_from_state(&state, &tdf);

	zassert_equal(state.reason, tdf.reason);
	zassert_equal(state.uptime, tdf.uptime);
	zassert_equal(state.hardware_reason, tdf.hardware_flags);
	zassert_equal(state.info.generic.info1, tdf.param_1);
	zassert_equal(state.info.generic.info2, tdf.param_2);

	/* Watchdog storage */
	state.reason = INFUSE_REBOOT_HW_WATCHDOG;
	state.info_type = INFUSE_REBOOT_INFO_WATCHDOG;
	state.info.watchdog.info1 = 0x4321;
	state.info.watchdog.info2 = 0x5432;
	tdf_reboot_info_from_state(&state, &tdf);

	zassert_equal(state.reason, tdf.reason);
	zassert_equal(state.uptime, tdf.uptime);
	zassert_equal(state.hardware_reason, tdf.hardware_flags);
	zassert_equal(state.info.watchdog.info1, tdf.param_1);
	zassert_equal(state.info.watchdog.info2, tdf.param_2);

	/* Exception basic storage */
	state.reason = K_ERR_CPU_EXCEPTION;
	state.info_type = INFUSE_REBOOT_INFO_EXCEPTION_BASIC;
	state.info.exception_basic.program_counter = 0x4567;
	state.info.exception_basic.link_register = 0x5678;
	tdf_reboot_info_from_state(&state, &tdf);

	zassert_equal(state.reason, tdf.reason);
	zassert_equal(state.uptime, tdf.uptime);
	zassert_equal(state.hardware_reason, tdf.hardware_flags);
	zassert_equal(state.info.exception_basic.program_counter, tdf.param_1);
	zassert_equal(state.info.exception_basic.link_register, tdf.param_2);

	/* Exception ESF storage */
#ifdef CONFIG_ARM
	state.reason = K_ERR_ARCH_START;
	state.info_type = INFUSE_REBOOT_INFO_EXCEPTION_ESF;
	state.info.exception_full.basic.pc = 0xABCD;
	state.info.exception_full.basic.lr = 0xBCDE;
	tdf_reboot_info_from_state(&state, &tdf);

	zassert_equal(state.reason, tdf.reason);
	zassert_equal(state.uptime, tdf.uptime);
	zassert_equal(state.hardware_reason, tdf.hardware_flags);
	zassert_equal(state.info.exception_full.basic.pc, tdf.param_1);
	zassert_equal(state.info.exception_full.basic.lr, tdf.param_2);
#endif /* CONFIG_ARM */
#ifdef CONFIG_ARCH_POSIX
	state.reason = K_ERR_ARCH_START;
	state.info_type = INFUSE_REBOOT_INFO_EXCEPTION_ESF;
	state.info.exception_full.dummy = 0x12345678;
	tdf_reboot_info_from_state(&state, &tdf);

	zassert_equal(state.reason, tdf.reason);
	zassert_equal(state.uptime, tdf.uptime);
	zassert_equal(state.hardware_reason, tdf.hardware_flags);
	zassert_equal(0x00, tdf.param_1);
	zassert_equal(0x00, tdf.param_2);
#endif /* CONFIG_ARCH_POSIX*/
}

ZTEST_SUITE(tdf_util, NULL, NULL, NULL, NULL, NULL);
