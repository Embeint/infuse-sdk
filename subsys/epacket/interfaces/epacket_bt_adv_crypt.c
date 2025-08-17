/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/sys/byteorder.h>

#include <infuse/epacket/keys.h>
#include <infuse/epacket/interface/epacket_bt.h>
#include <infuse/security.h>

#include "epacket_internal.h"

#define EMBEINT_COMPANY_CODE 0x0DE4
#define BT_MFG_DATA_LEN      103

static struct {
	uint16_t company_code;
	uint8_t payload[BT_MFG_DATA_LEN];
} __packed mfg_data;

/* Maximum serialized data structure length is 124 bytes in order to be
 * received by iOS devices. Layout:
 *    Extended Advertising Header = 10 bytes
 *    AD Structures:
 *                          Flags = (2 + 1) bytes
 *                   Service UUID = (2 + 2) bytes
 *              Manufacturer Data = (2 + 2 + 103) bytes
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
	BT_DATA_BYTES(BT_DATA_UUID16_SOME, BT_UUID_16_ENCODE(INFUSE_BT_SERVICE_UUID_VAL)),
	/* Manufacturer specific data.
	 * First 2 bytes are the Company Identifier Code from:
	 *   https://www.bluetooth.com/specifications/assigned-numbers/
	 * Remainder is arbitrary binary payload.
	 */
	BT_DATA(BT_DATA_MANUFACTURER_DATA, &mfg_data, sizeof(mfg_data)),
};

void epacket_bt_adv_ad_init(void)
{
	mfg_data.company_code = sys_cpu_to_le16(EMBEINT_COMPANY_CODE);
}

void *epacket_bt_adv_pkt_to_ad(struct net_buf *pkt, size_t *num)
{
	/* Copy payload into data structure */
	memcpy(mfg_data.payload, pkt->data, pkt->len);
	/* Manufacturer ID + payload */
	ad_structures[2].data_len = sizeof(mfg_data.company_code) + pkt->len;

	*num = ARRAY_SIZE(ad_structures);
	return ad_structures;
}

bool epacket_bt_adv_is_epacket(uint8_t adv_type, struct net_buf_simple *buf)
{
	/* Infuse packets are always extended advertising */
	if (adv_type != BT_GAP_ADV_TYPE_EXT_ADV) {
		return false;
	}
	/* First field is always BT_DATA_FLAGS */
	if (buf->data[0] != 2 || buf->data[1] != BT_DATA_FLAGS) {
		return false;
	}
	/* Second field is always BT_DATA_UUID16_SOME */
	if (buf->data[3] != 3 || buf->data[4] != BT_DATA_UUID16_SOME) {
		return false;
	}
	/* Third field is always BT_DATA_MANUFACTURER_DATA */
	if (buf->data[8] != BT_DATA_MANUFACTURER_DATA) {
		return false;
	}
	/* Manufacturer ID is EMBEINT_COMPANY_CODE */
	if (sys_get_le16(buf->data + 9) != EMBEINT_COMPANY_CODE) {
		return false;
	}
	/* Remove Bluetooth advertising headers */
	net_buf_simple_pull(buf, 11);
	return true;
}

int epacket_bt_adv_encrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_BT_ADV,
					    infuse_security_network_key_identifier());
}

int epacket_bt_adv_decrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_BT_ADV);
}
