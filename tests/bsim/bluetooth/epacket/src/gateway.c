/**
 * Copyright (c) 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "common.h"

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>

extern enum bst_result_t bst_result;
static K_SEM_DEFINE(packet_received, 0, 1);
static atomic_t received_packets;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void epacket_bt_adv_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_INF("RX Type: %02X Flags: %04X Auth: %d Len: %d RSSI: %ddBm", meta->type, meta->flags,
		meta->auth, buf->len, meta->rssi);
	atomic_inc(&received_packets);

	net_buf_unref(buf);
}

static void main_gateway_scan(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	int rc;

	epacket_set_receive_handler(epacket_bt_adv, epacket_bt_adv_receive_handler);
	rc = epacket_receive(epacket_bt_adv, K_FOREVER);
	if (rc < 0) {
		FAIL("Failed to start ePacket receive (%d)\n", rc);
		return;
	}

	LOG_INF("Waiting for packets");
	k_sleep(K_SECONDS(9));

	rc = epacket_receive(epacket_bt_adv, K_NO_WAIT);
	if (rc < 0) {
		FAIL("Failed to stop ePacket receive (%d)\n", rc);
		return;
	}

	if (atomic_get(&received_packets) < 10) {
		FAIL("Failed to receive expected packets\n");
	} else {
		PASS("Received %d packets from advertiser\n", atomic_get(&received_packets));
	}
}

static const struct bst_test_instance epacket_gateway[] = {
	{.test_id = "epacket_bt_gateway_scan",
	 .test_descr = "Scans for advertising ePackets on advertising PHY",
	 .test_pre_init_f = test_init,
	 .test_tick_f = test_tick,
	 .test_main_f = main_gateway_scan},
	BSTEST_END_MARKER};

struct bst_test_list *test_epacket_bt_gateway(struct bst_test_list *tests)
{
	return bst_add_tests(tests, epacket_gateway);
}

bst_test_install_t test_installers[] = {test_epacket_bt_gateway, NULL};

int main(void)
{
	bst_main();
	return 0;
}
