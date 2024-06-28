/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>

#include <infuse/identifiers.h>
#include <infuse/time/civil.h>
#include <infuse/epacket/interface.h>
#include <infuse/epacket/packet.h>
#include <infuse/epacket/interface/epacket_bt_adv.h>

#include "epacket_internal.h"

#define DT_DRV_COMPAT embeint_epacket_bt_adv

#define INFUSE_SERVICE_UUID  0xC001
#define EMBEINT_COMPANY_CODE 0xFFFF
#define BT_MFG_DATA_LEN      113

LOG_MODULE_REGISTER(epacket_bt_adv, CONFIG_EPACKET_BT_ADV_LOG_LEVEL);

static void adv_set_complete(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info);

static struct {
	uint16_t company_code;
	uint8_t payload[BT_MFG_DATA_LEN];
} __packed mfg_data;

/* Maximum serialized data structure length is 124 bytes in order to be
 * received by iOS devices. Layout:
 *                Flags = (2 + 1) bytes
 *         Service UUID = (2 + 2) bytes
 *    Manufacturer Data = (2 + 2 + 113) bytes
 */
static struct bt_data ad_structures[] = {
	/* From BT Core Specification Supplement v11:
	 *   The Flags data type shall be included when any of the Flag bits
	 *   are non-zero and the advertising packet is connectable, otherwise
	 *   the Flags data type may be omitted.
	 * Bits are non-zero and most packets will be connectable, therefore
	 * include the data type.
	 */
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	/* Background Bluetooth Advertising scanning on iOS requires a service
	 * UUID to be present:
	 *   https://developer.apple.com/documentation/corebluetooth/cbcentralmanager/scanforperipherals(withservices:options:)
	 */
	BT_DATA_BYTES(BT_DATA_UUID16_SOME, BT_UUID_16_ENCODE(INFUSE_SERVICE_UUID)),
	/* Manufacturer specific data.
	 * First 2 bytes are the Company Identifier Code from:
	 *   https://www.bluetooth.com/specifications/assigned-numbers/
	 * Remainder is arbitrary binary payload.
	 */
	BT_DATA(BT_DATA_MANUFACTURER_DATA, &mfg_data, sizeof(mfg_data)),
};
static const struct bt_le_ext_adv_cb adv_cb = {
	.sent = adv_set_complete,
};
static const struct bt_le_ext_adv_start_param adv_start_param = {
	.timeout = 0,
	.num_events = 1,
};
static struct net_buf *adv_set_bufs[CONFIG_BT_EXT_ADV_MAX_ADV_SET];
static struct k_spinlock queue_lock;
static K_FIFO_DEFINE(tx_buf_queue);

static void bt_adv_broadcast(const struct device *dev, struct bt_le_ext_adv *adv,
			     struct net_buf *pkt)
{
	int rc;

	/* Store net buf for callback */
	adv_set_bufs[bt_le_ext_adv_get_index(adv)] = pkt;

	/* Copy payload into data structure */
	memcpy(mfg_data.payload, pkt->data, pkt->len);
	ad_structures[2].data_len = pkt->len;

	/* Set extended advertising data */
	rc = bt_le_ext_adv_set_data(adv, ad_structures, ARRAY_SIZE(ad_structures), NULL, 0);
	if (rc != 0) {
		LOG_ERR("Failed to set advertising data (%d)", rc);
		goto end;
	}

	/* Start transmitting the data */
	rc = bt_le_ext_adv_start(adv, &adv_start_param);
	if (rc != 0) {
		LOG_ERR("Failed to start advertising set (%d)", rc);
	}

end:
	if (rc != 0) {
		/* Handle failures to advertise */
		(void)bt_le_ext_adv_delete(adv);
		epacket_notify_tx_result(dev, pkt, rc);
		net_buf_unref(pkt);
	}
}

static void adv_set_complete(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	uint8_t set_idx = bt_le_ext_adv_get_index(adv);
	struct net_buf *curr, *next;
	int rc;

	curr = adv_set_bufs[set_idx];
	adv_set_bufs[set_idx] = NULL;

	K_SPINLOCK(&queue_lock) {
		next = net_buf_get(&tx_buf_queue, K_NO_WAIT);
		if (next) {
			LOG_DBG("Chaining next buf: %p", next);
			bt_adv_broadcast(DEVICE_DT_INST_GET(0), adv, next);
		} else {
			LOG_DBG("Deleting set %d", set_idx);
			rc = bt_le_ext_adv_delete(adv);
			if (rc != 0) {
				LOG_WRN("Failed to delete set %d", set_idx);
			}
		}
	}

	/* Notify TX result */
	epacket_notify_tx_result(DEVICE_DT_INST_GET(0), curr, 0);
	net_buf_unref(curr);
}

static void epacket_bt_adv_send(const struct device *dev, struct net_buf *buf)
{
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_1,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_1,
		.peer = NULL,
	};
	struct bt_le_ext_adv *adv;
	int rc;

	/* Encrypt the payload */
	if (epacket_bt_adv_encrypt(buf) < 0) {
		LOG_WRN("Failed to encrypt");
		epacket_notify_tx_result(dev, buf, -EIO);
		return;
	}

	/* Create advertising set */
	K_SPINLOCK(&queue_lock) {
		rc = bt_le_ext_adv_create(&adv_param, &adv_cb, &adv);
		if (rc == -ENOMEM) {
			LOG_DBG("Queueing buf %p", buf);
			net_buf_put(&tx_buf_queue, buf);
		} else if (rc < 0) {
			LOG_ERR("Failed to create advertising set (%d)", rc);
			epacket_notify_tx_result(dev, buf, rc);
			net_buf_unref(buf);
		}
	}

	if (rc == 0) {
		/* Broadcast on the set on success */
		bt_adv_broadcast(dev, adv, buf);
	}
}

static int epacket_bt_adv_init(const struct device *dev)
{
	epacket_interface_common_init(dev);
	k_fifo_init(&tx_buf_queue);
	mfg_data.company_code = sys_cpu_to_be16(EMBEINT_COMPANY_CODE);

	return 0;
}

static const struct epacket_interface_api bt_adv_api = {
	.send = epacket_bt_adv_send,
};

BUILD_ASSERT(sizeof(mfg_data.payload) == DT_INST_PROP(0, max_packet_size));
static struct epacket_interface_common_data epacket_bt_adv_data;
static const struct epacket_interface_common_config epacket_bt_adv_config = {
	.max_packet_size = EPACKET_INTERFACE_MAX_PACKET(DT_DRV_INST(0)),
	.header_size = DT_INST_PROP(0, header_size),
	.footer_size = DT_INST_PROP(0, footer_size),
};
DEVICE_DT_DEFINE(DT_DRV_INST(0), epacket_bt_adv_init, NULL, &epacket_bt_adv_data,
		 &epacket_bt_adv_config, POST_KERNEL, 0, &bt_adv_api);
