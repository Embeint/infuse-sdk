/**
 * Copyright (c) 2024 Embeint Holdings Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/pm/device_runtime.h>

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"

#include <infuse/epacket/interface.h>
#include <infuse/epacket/interface/epacket_bt_central.h>
#include <infuse/epacket/interface/epacket_dummy.h>
#include <infuse/epacket/packet.h>
#include <infuse/bluetooth/gatt.h>
#include <infuse/rpc/types.h>
#include <infuse/rpc/client.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/tdf/definitions.h>

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, "PASSED: " __VA_ARGS__);                                     \
	} while (0)

#define WAIT_SECONDS 30                            /* seconds */
#define WAIT_TIME    (WAIT_SECONDS * USEC_PER_SEC) /* microseconds*/

extern enum bst_result_t bst_result;
static K_SEM_DEFINE(epacket_serial_received, 0, 1);
static atomic_t received_packets;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static void epacket_serial_receive_handler(struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);

	LOG_INF("%3d: RX Type: %02X Flags: %04X Auth: %d Len: %d RSSI: %ddBm", meta->sequence,
		meta->type, meta->flags, meta->auth, buf->len, meta->rssi);
	atomic_inc(&received_packets);

	net_buf_unref(buf);

	k_sem_give(&epacket_serial_received);
}

static void main_serial_loopback(void)
{
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	const struct device *serial_dev =
		DEVICE_DT_GET(DT_PROP(DT_NODELABEL(epacket_serial), serial));
	struct tdf_announce announce = {0};
	int rc;

	epacket_set_receive_handler(epacket_serial, epacket_serial_receive_handler);
	rc = epacket_receive(epacket_serial, K_FOREVER);
	if (rc < 0) {
		FAIL("Failed to start ePacket receive (%d)\n", rc);
		return;
	}

	for (int i = 0; i < 5; i++) {
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_SERIAL, TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_SERIAL, TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
		k_sleep(K_MSEC(250));
	}

	if (received_packets != 10) {
		FAIL("Failed to receive looped serial ePackets\n");
		return;
	}

	/* Disable reception */
	rc = epacket_receive(epacket_serial, K_NO_WAIT);
	if (rc < 0) {
		FAIL("Failed to stop ePacket receive (%d)\n", rc);
		return;
	}

	for (int i = 0; i < 1; i++) {
		TDF_DATA_LOGGER_LOG(TDF_DATA_LOGGER_SERIAL, TDF_ANNOUNCE, 0, &announce);
		tdf_data_logger_flush(TDF_DATA_LOGGER_SERIAL);
		k_sleep(K_MSEC(250));
	}

	if (received_packets != 10) {
		FAIL("Received packets after RX disabled\n");
		return;
	}

#ifdef CONFIG_PM_DEVICE_RUNTIME
	if (pm_device_runtime_usage(serial_dev) != 0) {
		FAIL("Serial instance not idle at test completion\n");
		return;
	}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

	PASS("Loopback test passed\n");
}

static void main_serial_tx_timeout(void)
{
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	struct net_buf *buf;

	/* Queue several buffers that we know can't fit atomically in the FIFO buffer,
	 * and will therefore be punted to the TX timeout handler.
	 */
	for (int i = 0; i < 3; i++) {
		buf = epacket_alloc_tx_for_interface(epacket_serial, K_NO_WAIT);
		if (buf == NULL) {
			FAIL("Failed to allocate TX buffer\n");
			return;
		}
		epacket_set_tx_metadata(buf, EPACKET_AUTH_NETWORK, 0, INFUSE_TDF, EPACKET_ADDR_ALL);
		net_buf_add(buf, 256);

		epacket_queue(epacket_serial, buf);
	}
	k_sleep(K_SECONDS(1));

	/* Buffers should have been reclaimed by the TX timeout handler */
	for (int i = 0; i < CONFIG_EPACKET_BUFFERS_TX; i++) {
		buf = epacket_alloc_tx_for_interface(epacket_serial, K_NO_WAIT);
		if (buf == NULL) {
			FAIL("Failed to allocate TX buffer\n");
			return;
		}
		/* Don't bother trying to free the buffers afterwards, as the test is terminating */
	}

#ifdef CONFIG_PM_DEVICE_RUNTIME
	const struct device *serial_dev =
		DEVICE_DT_GET(DT_PROP(DT_NODELABEL(epacket_serial), serial));

	if (pm_device_runtime_usage(serial_dev) != 0) {
		FAIL("Serial instance not idle at test completion\n");
		return;
	}
#endif /* CONFIG_PM_DEVICE_RUNTIME */

	PASS("TX timeout test passed\n");
}

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i seconds)\n", WAIT_SECONDS);
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
	bst_result = In_progress;
}

static const struct bst_test_instance epacket_serial_tests[] = {
	{
		.test_id = "epacket_serial_loopback",
		.test_descr = "Send serial packets",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_serial_loopback,
	},
	{
		.test_id = "epacket_serial_tx_timeout",
		.test_descr = "Queue packets that can't be sent",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = main_serial_tx_timeout,
	},
	BSTEST_END_MARKER};

struct bst_test_list *test_epacket_serial(struct bst_test_list *tests)
{
	return bst_add_tests(tests, epacket_serial_tests);
}

bst_test_install_t test_installers[] = {test_epacket_serial, NULL};

int main(void)
{
	bst_main();
	return 0;
}
