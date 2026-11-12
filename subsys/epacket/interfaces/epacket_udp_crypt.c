/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/types.h>
#include <infuse/epacket/keys.h>
#include <infuse/epacket/packet.h>
#include <infuse/security.h>

#include "epacket_internal.h"

int epacket_udp_encrypt(struct net_buf *buf)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	/* Upgrade network auth to device auth, except for RPC responses */
	if (IS_ENABLED(CONFIG_EPACKET_INTERFACE_UDP_DEVICE_AUTH_PREFERRED) &&
	    (meta->auth == EPACKET_AUTH_NETWORK) && (meta->type != INFUSE_RPC_RSP)) {
		meta->auth = EPACKET_AUTH_DEVICE;
		meta->key_identifier = infuse_security_device_key_identifier();
	}

	return epacket_unversioned_v0_encrypt(buf, EPACKET_KEY_INTERFACE_UDP);
}

int epacket_udp_decrypt(struct net_buf *buf)
{
	return epacket_unversioned_v0_decrypt(buf, EPACKET_KEY_INTERFACE_UDP);
}

int epacket_udp_tx_decrypt(struct net_buf *buf)
{
	return epacket_unversioned_v0_tx_decrypt(buf, EPACKET_KEY_INTERFACE_UDP);
}
