/**
 * @file
 * @brief ePacket filtering functions
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_FILTER_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_FILTER_H_

#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket filter API
 * @defgroup epacket_filter_apis ePacket filter APIs
 * @{
 */

enum epacket_filter_flags {
	/** Only forward packets that successfully decrypted */
	FILTER_FORWARD_ONLY_DECRYPTED = BIT(0),
	/** Only forward TDF packets */
	FILTER_FORWARD_ONLY_TDF = BIT(1),
	/** Only forward TDF packets that contain a TDF_ANNOUNCE or TDF_ANNOUNCE_V2 reading.
	 * Implies ONLY_DECRYPTED and ONLY_TDF)
	 */
	FILTER_FORWARD_ONLY_TDF_ANNOUNCE = BIT(2),
	/** If filtering fails, application should forward the RSSI */
	FILTER_FORWARD_RSSI_FALLBACK = BIT(3),
};

/**
 * @brief Determine whether a packet should be forwarded
 *
 * @param flags Criteria from @ref epacket_filter_flags for forwarding packet
 * @param percent Percent of packets to forward that pass @a flags (255 = all, 128 = half, 0 = none)
 * @param buf ePacket that was received
 *
 * @return true Packet should be forwarded
 * @return false Packet should be dropped
 */
bool epacket_gateway_forward_filter(uint8_t flags, uint8_t percent, struct net_buf *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_FILTER_H_ */
