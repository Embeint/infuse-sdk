/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>

#include <infuse/reboot.h>
#include <infuse/work_q.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>
#include <infuse/identifiers.h>
#include <infuse/task_runner/runner.h>
#include <infuse/time/epoch.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_bt_adv

#define SCAN_WDOG_TIMEOUT K_SECONDS(CONFIG_EPACKET_INTERFACE_BT_ADV_SCAN_WATCHDOG_SEC)

LOG_MODULE_REGISTER(epacket_bt_adv, CONFIG_EPACKET_BT_ADV_LOG_LEVEL);

static void adv_set_complete(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info);

static const struct bt_le_ext_adv_cb adv_cb = {
	.sent = adv_set_complete,
};
static const struct bt_le_ext_adv_start_param adv_start_param = {
	.timeout = 0,
	.num_events = 1,
};
static const struct bt_le_scan_param scan_param = {
	.type = BT_LE_SCAN_TYPE_PASSIVE,
	.options = BT_LE_SCAN_OPT_NONE,
	.interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
	.window = BT_GAP_SCAN_FAST_WINDOW,
};
static struct k_work_delayable scan_watchdog_work;
static uint32_t scan_watchdog_timeouts;
static struct net_buf *adv_set_bufs[CONFIG_BT_EXT_ADV_MAX_ADV_SET];
static K_FIFO_DEFINE(tx_buf_queue);
static struct bt_le_ext_adv *adv_set;
static bool adv_set_active;
static uint8_t scan_suspended;
static K_SEM_DEFINE(scan_control, 1, 1);

#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV_FALLBACK_SCAN_CALLBACK
static bt_le_scan_cb_t *fallback_scan_cb;

void epacket_bt_adv_set_fallback_scan_callback(bt_le_scan_cb_t scan_cb)
{
	fallback_scan_cb = scan_cb;
}

#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV_FALLBACK_SCAN_CALLBACK */

#if CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC > 0

static void connectable_wdog_expiry(struct k_work *work)
{
#ifdef CONFIG_INFUSE_REBOOT
	LOG_WRN("Connectable advertising watchdog expired, rebooting in 2 seconds...");
	infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG, (uintptr_t)connectable_wdog_expiry,
			      CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC,
			      K_SECONDS(2));
#else
	LOG_ERR("Connectable advertising watchdog expired, reboot not supported...");
#endif
}

static K_WORK_DELAYABLE_DEFINE(connectable_wdog, connectable_wdog_expiry);

#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC > 0 */

static void bt_adv_broadcast(const struct device *dev, struct net_buf *pkt)
{
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_1,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_1,
		.peer = NULL,
	};
	struct bt_data *ad_data;
	size_t num_ad;
	int rc;

	/* Create the advertising set if it doesn't exist */
	if (adv_set == NULL) {
		rc = bt_le_ext_adv_create(&adv_param, &adv_cb, &adv_set);
		if (rc != 0) {
			LOG_ERR("Failed to create advertising set (%d)", rc);
			goto end;
		}
	}

	/* Store net buf for callback */
	adv_set_bufs[bt_le_ext_adv_get_index(adv_set)] = pkt;

	/* Serialize into AD structures */
	ad_data = epacket_bt_adv_pkt_to_ad(pkt, &num_ad);

	/* Set extended advertising data */
	rc = bt_le_ext_adv_set_data(adv_set, ad_data, num_ad, NULL, 0);
	if (rc != 0) {
		LOG_ERR("Failed to set advertising data (%d)", rc);
		goto end;
	}

	/* Try connectable by default */
	rc = bt_le_ext_adv_update_param(adv_set, &adv_param);
	if (rc != 0) {
		LOG_ERR("Failed to update params (%d)", rc);
		goto end;
	}

	/* Start transmitting the data */
	rc = bt_le_ext_adv_start(adv_set, &adv_start_param);
	if (rc == 0) {
#if CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC > 0
		/* Connectable advertising started, reset watchdog */
		k_work_reschedule(
			&connectable_wdog,
			K_SECONDS(CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC));
#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC */
	} else if (rc == -ENOMEM) {
		/* No Bluetooth connections left, clear connectable flag */
		adv_param.options ^= BT_LE_ADV_OPT_CONN;
		rc = bt_le_ext_adv_update_param(adv_set, &adv_param);
		if (rc != 0) {
			LOG_ERR("Failed to update params (%d)", rc);
			goto end;
		}
		/* Try again */
		rc = bt_le_ext_adv_start(adv_set, &adv_start_param);
	}
	if (rc != 0) {
		LOG_ERR("Failed to start advertising set (%d)", rc);
	}
	adv_set_active = true;

end:
	if (rc != 0) {
		/* Handle failures to advertise */
		if (adv_set != NULL) {
			(void)bt_le_ext_adv_delete(adv_set);
		}
		epacket_notify_tx_result(dev, pkt, rc);
		net_buf_unref(pkt);
		adv_set = NULL;
		adv_set_active = false;
	}
}

void epacket_bt_adv_send_next(void)
{
	struct net_buf *next;

	next = k_fifo_get(&tx_buf_queue, K_NO_WAIT);
	if (next) {
		LOG_DBG("Chaining next buf: %p", next);
		bt_adv_broadcast(DEVICE_DT_INST_GET(0), next);
	} else {
		LOG_DBG("Adv chain complete");
		adv_set_active = false;
	}
}

static void adv_set_complete(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	uint8_t set_idx = bt_le_ext_adv_get_index(adv);
	struct net_buf *curr;

	/* Release the finished buffer immediately */
	curr = adv_set_bufs[set_idx];
	adv_set_bufs[set_idx] = NULL;

	/* Notify TX result */
	epacket_notify_tx_result(DEVICE_DT_INST_GET(0), curr, 0);
	net_buf_unref(curr);

	/* Notify processing thread that epacket_bt_adv_send_next should be called */
	epacket_bt_adv_send_next_trigger();
}

static void epacket_bt_adv_send(const struct device *dev, struct net_buf *buf)
{
	/* Encrypt the payload */
	if (epacket_bt_adv_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		net_buf_unref(buf);
		return;
	}

	if (adv_set_active) {
		/* Queue buffer for transmission if already active */
		LOG_DBG("Queueing buf %p", buf);
		k_fifo_put(&tx_buf_queue, buf);
	} else {
		/* Broadcast if not active */
		bt_adv_broadcast(dev, buf);
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	const char *bt_addr_le_str(const bt_addr_le_t *addr);
	struct epacket_rx_metadata *meta;
	struct net_buf *rx_buffer;

	/* Advertising packet observed, reset watchdog */
	infuse_work_reschedule(&scan_watchdog_work, SCAN_WDOG_TIMEOUT);
	scan_watchdog_timeouts = 0;

	if (!epacket_bt_adv_is_epacket(adv_type, buf)) {
#ifdef CONFIG_EPACKET_INTERFACE_BT_ADV_FALLBACK_SCAN_CALLBACK
		if (fallback_scan_cb != NULL) {
			fallback_scan_cb(addr, rssi, adv_type, buf);
		}
#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV_FALLBACK_SCAN_CALLBACK */
		return;
	}
	LOG_DBG("%s: %d bytes %d dBm", bt_addr_le_str(addr), buf->len, rssi);

	/* Allocate RX buffer.
	 * Bluetooth advertising is best effort and this function executes from
	 * the Bluetooth stack so don't wait for a buffer
	 */
	rx_buffer = epacket_alloc_rx(K_NO_WAIT);
	if (rx_buffer == NULL) {
		LOG_WRN("Dropping packet from %s", bt_addr_le_str(addr));
		return;
	}

	/* Copy payload across */
	net_buf_add_mem(rx_buffer, buf->data, buf->len);

	/* Save metadata */
	meta = net_buf_user_data(rx_buffer);
	meta->interface = DEVICE_DT_INST_GET(0);
	meta->interface_id = EPACKET_INTERFACE_BT_ADV;
	meta->interface_address.bluetooth = *addr;
	meta->rssi = rssi;

	/* Hand off to ePacket core */
	epacket_raw_receive_handler(rx_buffer);
}

static int epacket_bt_adv_receive_control(const struct device *dev, bool enable)
{
	int rc = 0;

	k_sem_take(&scan_control, K_FOREVER);
	if (enable) {
		if (scan_suspended == 0) {
			/* Scanning has been temporarily blocked */
			rc = bt_le_scan_start(&scan_param, scan_cb);
		}
		infuse_work_reschedule(&scan_watchdog_work, SCAN_WDOG_TIMEOUT);
	} else {
		k_work_cancel_delayable(&scan_watchdog_work);
		if (scan_suspended == 0) {
			/* Scanning has already been stopped */
			rc = bt_le_scan_stop();
		}
	}
	k_sem_give(&scan_control);
	return rc;
}

static void scan_rx_watchdog_expired(struct k_work *work)
{
	const uint32_t reboot_age =
		CONFIG_EPACKET_INTERFACE_BT_ADV_SCAN_WATCHDOG_MIN_REBOOT_AGE_SEC;
	int rc;

	LOG_WRN("Scan RX watchdog expired, restarting scan");
	rc = bt_le_scan_stop();
	if (rc != 0) {
		LOG_ERR("Failed to stop scanning (%d)", rc);
	}
	rc = bt_le_scan_start(&scan_param, scan_cb);
	if (rc != 0) {
		LOG_ERR("Failed to restart scanning (%d)", rc);
	}

	/* Another timeout without any scan results */
	scan_watchdog_timeouts += 1;
	if ((scan_watchdog_timeouts >= 2) && (k_uptime_seconds() > reboot_age)) {
#ifdef CONFIG_INFUSE_REBOOT
		infuse_reboot_delayed(INFUSE_REBOOT_SW_WATCHDOG,
				      (uintptr_t)scan_rx_watchdog_expired, scan_watchdog_timeouts,
				      K_SECONDS(2));
#else
		LOG_WRN("INFUSE_REBOOT not supported");
#endif
	} else {
		/* Restart the watchdog */
		infuse_work_reschedule(&scan_watchdog_work, SCAN_WDOG_TIMEOUT);
	}
}

void epacket_bt_adv_scan_suspend(void)
{
	int rc;

	k_sem_take(&scan_control, K_FOREVER);
	if (k_work_delayable_is_pending(&scan_watchdog_work) && (scan_suspended == 0)) {
		/* Scanning is currently ongoing, cancel it */
		LOG_INF("Suspending scanning");
		rc = bt_le_scan_stop();
		if (rc != 0) {
			LOG_ERR("Failed to stop scanning (%d)", rc);
		}
	}
	scan_suspended += 1;
	k_sem_give(&scan_control);
}

void epacket_bt_adv_scan_resume(void)
{
	int rc;

	k_sem_take(&scan_control, K_FOREVER);
	scan_suspended -= 1;
	if (k_work_delayable_is_pending(&scan_watchdog_work) && (scan_suspended == 0)) {
		/* Scanning is still desired by the application */
		LOG_INF("Resuming scanning");
		rc = bt_le_scan_start(&scan_param, scan_cb);
		if (rc != 0) {
			LOG_ERR("Failed to restart scanning (%d)", rc);
		}
	}
	k_sem_give(&scan_control);
}

static int epacket_bt_adv_init(const struct device *dev)
{
	k_work_init_delayable(&scan_watchdog_work, scan_rx_watchdog_expired);
	epacket_interface_common_init(dev);
	epacket_bt_adv_ad_init();
	k_fifo_init(&tx_buf_queue);

#if CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC > 0
	/* Start connectable watchdog on boot */
	k_work_reschedule(&connectable_wdog,
			  K_SECONDS(CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC));
#endif /* CONFIG_EPACKET_INTERFACE_BT_ADV_CONNECTABLE_WATCHDOG_SEC */

	return 0;
}

static const struct epacket_interface_api bt_adv_api = {
	.send = epacket_bt_adv_send,
	.receive_ctrl = epacket_bt_adv_receive_control,
};

BUILD_ASSERT(103 == DT_INST_PROP(0, max_packet_size));
static struct epacket_interface_common_data epacket_bt_adv_data;
static const struct epacket_interface_common_config epacket_bt_adv_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_bt_adv_init, NULL, &epacket_bt_adv_data,
		 &epacket_bt_adv_config, POST_KERNEL, 0, &bt_adv_api);
