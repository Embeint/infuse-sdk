/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/epacket/keys.h>

#include "epacket_internal.h"

int epacket_bt_gatt_encrypt(struct net_buf *buf, uint32_t network_key_id)
{
	return epacket_versioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT, network_key_id);
}

int epacket_bt_gatt_decrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT);
}
