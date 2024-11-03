/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

extern enum bst_result_t bst_result;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void main_epacket_bt_basic_broadcast(void)
{
	struct tdf_announce announce = {0};

	LOG_INF("Starting send");

	/* Burst send some packets */
	for (int i = 0; i < 5; i++) {
		tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, (sizeof(announce)), 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
	}
	k_sleep(K_MSEC(500));

	/* Send 5 packets with spacing */
	for (int i = 0; i < 8; i++) {
		k_sleep(K_MSEC(1000));
		LOG_INF("TX %d", i);
		announce.uptime = k_uptime_seconds();
		tdf_data_logger_log(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, (sizeof(announce)), 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
	}
	k_sleep(K_MSEC(500));

	PASS("Advertising device complete\n");
}

static const struct bst_test_instance ext_adv_advertiser[] = {
	{.test_id = "epacket_bt_device",
	 .test_descr = "Basic Infuse-IoT Bluetooth device",
	 .test_pre_init_f = test_init,
	 .test_tick_f = test_tick,
	 .test_main_f = main_epacket_bt_basic_broadcast},
	BSTEST_END_MARKER};

struct bst_test_list *test_ext_adv_advertiser(struct bst_test_list *tests)
{
	return bst_add_tests(tests, ext_adv_advertiser);
}

bst_test_install_t test_installers[] = {test_ext_adv_advertiser, NULL};

int main(void)
{
	bst_main();
	return 0;
}
