/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <stdio.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>

#include <infuse/bluetooth/legacy_adv.h>
#include <infuse/data_logger/high_level/tdf.h>
#include <infuse/drivers/watchdog.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/work_q.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static atomic_t raw_hci_sent;

static void bt_hci_send(void)
{
	struct net_buf *rsp;
	int err;

	/* Read Local Version Information */
	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_LOCAL_VERSION_INFO, NULL, &rsp);
	if (err) {
		LOG_ERR("HCI CMD ERR: %d", err);
		return;
	}
	net_buf_unref(rsp);

	atomic_inc(&raw_hci_sent);
}

static void scan_handler(struct net_buf *buf)
{
	atomic_val_t raw_sent = atomic_clear(&raw_hci_sent);

	LOG_INF("Raw HCI: %d", (int)raw_sent);
	net_buf_unref(buf);
}

static void gatt_pusher(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tdf_acc_2g acc_array[32] = {0};

	TDF_DATA_LOGGER_LOG_ARRAY(TDF_DATA_LOGGER_BT_PERIPHERAL, TDF_ACC_2G, ARRAY_SIZE(acc_array),
				  0, 100, acc_array);
	tdf_data_logger_flush(TDF_DATA_LOGGER_BT_PERIPHERAL);

	infuse_work_reschedule(dwork, K_SECONDS(1));
}

int main(void)
{
	const struct device *epacket_bt_adv = DEVICE_DT_GET(DT_NODELABEL(epacket_bt_adv));
	const struct device *epacket_serial = DEVICE_DT_GET(DT_NODELABEL(epacket_serial));
	struct k_work_delayable pusher;

	/* Start watchdog */
	infuse_watchdog_start();

	k_work_init_delayable(&pusher, gatt_pusher);
	infuse_work_reschedule(&pusher, K_SECONDS(1));

	/* Receive handlers */
	epacket_set_receive_handler(epacket_bt_adv, scan_handler);

	/* Always listening on Bluetooth advertising and serial */
	epacket_receive(epacket_serial, K_FOREVER);
	epacket_receive(epacket_bt_adv, K_FOREVER);

	/* Start legacy advertising to load the system some more */
	bluetooth_legacy_advertising_run();

	while (true) {
		bt_hci_send();
	}

	LOG_INF("BOOTED");
	k_sleep(K_FOREVER);
}
