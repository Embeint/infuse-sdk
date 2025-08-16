/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/conn.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

static void bt_conn_connected(struct bt_conn *conn, uint8_t err);

extern enum bst_result_t bst_result;
static int connection_count;
static struct k_work_delayable terminator;
static struct bt_conn *g_conn;

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = bt_conn_connected,
};

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void bt_conn_connected(struct bt_conn *conn, uint8_t err)
{
	int terminate_delay = (connection_count * 50) + 10;

	g_conn = conn;
	LOG_INF("Terminating connection %d in %d ms", connection_count, terminate_delay);
	k_work_reschedule(&terminator, K_MSEC(terminate_delay));
	connection_count += 1;
}

static void terminator_fn(struct k_work *work)
{
	int rc;

	rc = bt_conn_disconnect(g_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	LOG_INF("Disconnect result: %d", rc);
}

static void peripheral_interface_state(uint16_t current_max_payload, void *user_ctx)
{
	LOG_INF("Peripheral: %s (Payload %d)",
		current_max_payload > 0 ? "Connected" : "Disconnected", current_max_payload);
}

static void main_epacket_conn_refuser(void)
{
	const struct device *epacket_bt_periph = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_peripheral));
	struct tdf_announce announce = {0};
	struct epacket_interface_cb interface_cb = {
		.interface_state = peripheral_interface_state,
	};

	k_work_init_delayable(&terminator, terminator_fn);
	epacket_register_callback(epacket_bt_periph, &interface_cb);

	LOG_INF("Starting connection terminator send");

	for (int i = 0; i < 36; i++) {
		k_sleep(K_MSEC(500));
		announce.uptime = k_uptime_seconds();
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL,
				    TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_BT_ADV | TDF_DATA_LOGGER_BT_PERIPHERAL);
	}
	k_sleep(K_MSEC(500));

	PASS("Connection terminator complete\n");
}

static const struct bst_test_instance conn_terminator[] = {
	{.test_id = "epacket_bt_conn_terminator",
	 .test_descr = "Automatically terminates connections after creation",
	 .test_pre_init_f = test_init,
	 .test_tick_f = test_tick,
	 .test_main_f = main_epacket_conn_refuser},
	BSTEST_END_MARKER};

struct bst_test_list *test_conn_terminator(struct bst_test_list *tests)
{
	return bst_add_tests(tests, conn_terminator);
}

bst_test_install_t test_installers[] = {test_conn_terminator, NULL};

int main(void)
{
	bst_main();
	return 0;
}
