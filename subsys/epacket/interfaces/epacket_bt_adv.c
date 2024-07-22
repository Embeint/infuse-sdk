/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>

#include <infuse/identifiers.h>
#include <infuse/time/epoch.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_bt_adv

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
	/* 32 * 0.625 = 20ms */
	.interval = 0x0020,
	.window = 0x0020,
};
static struct net_buf *adv_set_bufs[CONFIG_BT_EXT_ADV_MAX_ADV_SET];
static struct k_spinlock queue_lock;
static K_FIFO_DEFINE(tx_buf_queue);
static struct bt_le_ext_adv *adv_set;
static bool adv_set_active;

static void bt_adv_broadcast(const struct device *dev, struct net_buf *pkt)
{
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV |
			   BT_LE_ADV_OPT_CONNECTABLE,
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
	if (rc == -ENOMEM) {
		/* No Bluetooth connections left, clear connectable flag */
		adv_param.options ^= BT_LE_ADV_OPT_CONNECTABLE;
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

static void adv_set_complete(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	uint8_t set_idx = bt_le_ext_adv_get_index(adv);
	struct net_buf *curr, *next;

	curr = adv_set_bufs[set_idx];
	adv_set_bufs[set_idx] = NULL;

	K_SPINLOCK(&queue_lock) {
		next = net_buf_get(&tx_buf_queue, K_NO_WAIT);
		if (next) {
			LOG_DBG("Chaining next buf: %p", next);
			bt_adv_broadcast(DEVICE_DT_INST_GET(0), next);
		} else {
			LOG_DBG("Adv chain complete: %d", set_idx);
			adv_set_active = false;
		}
	}

	/* Notify TX result */
	epacket_notify_tx_result(DEVICE_DT_INST_GET(0), curr, 0);
	net_buf_unref(curr);
}

static void epacket_bt_adv_send(const struct device *dev, struct net_buf *buf)
{
	/* Encrypt the payload */
	if (epacket_bt_adv_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		return;
	}

	if (adv_set == NULL) {
		/* First broadcast is dropped with SoftDevice if scanning (DRGN-22705).
		 * Workaround is to queue the first broadcast twice.
		 */
		buf = net_buf_ref(buf);
		bt_adv_broadcast(dev, buf);
	}

	K_SPINLOCK(&queue_lock) {
		if (adv_set_active) {
			/* Queue buffer for transmission if already active */
			LOG_DBG("Queueing buf %p", buf);
			net_buf_put(&tx_buf_queue, buf);
		} else {
			/* Broadcast if not active */
			bt_adv_broadcast(dev, buf);
		}
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	const char *bt_addr_le_str(const bt_addr_le_t *addr);
	struct epacket_rx_metadata *meta;
	struct net_buf *rx_buffer;

	if (!epacket_bt_adv_is_epacket(adv_type, buf)) {
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
	int rc;

	if (enable) {
		rc = bt_le_scan_start(&scan_param, scan_cb);
	} else {
		rc = bt_le_scan_stop();
	}
	return rc;
}

static int epacket_bt_adv_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	epacket_bt_adv_ad_init();
	k_fifo_init(&tx_buf_queue);
	return 0;
}

static const struct epacket_interface_api bt_adv_api = {
	.send = epacket_bt_adv_send,
	.receive_ctrl = epacket_bt_adv_receive_control,
};

BUILD_ASSERT(113 == DT_INST_PROP(0, max_packet_size));
static struct epacket_interface_common_data epacket_bt_adv_data;
static const struct epacket_interface_common_config epacket_bt_adv_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_bt_adv_init, NULL, &epacket_bt_adv_data,
		 &epacket_bt_adv_config, POST_KERNEL, 0, &bt_adv_api);
