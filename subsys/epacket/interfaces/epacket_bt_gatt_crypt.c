/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/epacket/keys.h>

#include "epacket_internal.h"

int epacket_bt_gatt_encrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT);
}

int epacket_bt_gatt_decrypt(struct net_buf *buf)
{
	return epacket_versioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_BT_GATT);
}
