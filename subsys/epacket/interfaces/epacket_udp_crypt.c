/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/epacket/keys.h>

#include "epacket_internal.h"

int epacket_udp_encrypt(struct net_buf *buf)
{
	return epacket_unversioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_UDP);
}

int epacket_udp_decrypt(struct net_buf *buf)
{
	return epacket_unversioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_UDP);
}
