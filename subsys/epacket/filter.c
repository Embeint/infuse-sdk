/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#include <infuse/epacket/filter.h>
#include <infuse/epacket/packet.h>
#include <infuse/tdf/definitions.h>
#include <infuse/tdf/util.h>

#include <zephyr/random/random.h>

bool epacket_gateway_forward_filter(uint8_t flags, uint8_t percent, struct net_buf *buf)
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

		/* Try to find a TDF_ANNOUNCE or TDF_ANNOUNCE_V2 */
		struct tdf_buffer_state state;
		bool found = false;

		tdf_parse_start(&state, buf->data, buf->len);
		while (tdf_parse(&state, &tdf) == 0) {
			if ((tdf.tdf_id == TDF_ANNOUNCE) || (tdf.tdf_id == TDF_ANNOUNCE_V2)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	if (percent < UINT8_MAX) {
		/* Is this packet unlucky? */
		if (sys_rand8_get() > percent) {
			return false;
		}
	}
	/* Passed all the filters */
	return true;
}
