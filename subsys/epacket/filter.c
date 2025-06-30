/**
 * @file
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#include <infuse/epacket/filter.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

bool epacket_gateway_forward_filter(uint8_t flags, struct net_buf *buf)
{
	struct epacket_rx_metadata *meta = net_buf_user_data(buf);
	struct tdf_parsed tdf;

	if ((flags & (FILTER_FORWARD_ONLY_DECRYPTED | FILTER_FORWARD_ONLY_TDF_ANNOUNCE)) &&
	    ((meta->auth == EPACKET_AUTH_FAILURE) ||
	     (meta->auth == EPACKET_AUTH_REMOTE_ENCRYPTED))) {
		return false;
	}
	if ((flags & (FILTER_FORWARD_ONLY_TDF | FILTER_FORWARD_ONLY_TDF_ANNOUNCE)) &&
	    (meta->type != INFUSE_TDF)) {
		return false;
	}
	if (flags & FILTER_FORWARD_ONLY_TDF_ANNOUNCE) {
		/* We know the packet decrypted and is a TDF */
		__ASSERT_NO_MSG((meta->auth == EPACKET_AUTH_DEVICE) ||
				(meta->auth == EPACKET_AUTH_NETWORK));
		__ASSERT_NO_MSG(meta->type == INFUSE_TDF);

		/* Try to find a TDF_ANNOUNCE */
		if (!tdf_parse_find_in_buf(buf->data, buf->len, TDF_ANNOUNCE, &tdf) == 0) {
			return false;
		}
	}
	/* Passed all the filters */
	return true;
}
