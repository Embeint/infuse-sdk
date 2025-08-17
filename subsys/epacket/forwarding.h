/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_SUBSYS_EPACKET_FORWARDING_AUTO_CONN_H_
#define INFUSE_SDK_SUBSYS_EPACKET_FORWARDING_AUTO_CONN_H_

#include <zephyr/net_buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle forwarding packets
 *
 * @param buf packet to forward
 */
void epacket_packet_forward(struct net_buf *buf);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_SUBSYS_EPACKET_FORWARDING_AUTO_CONN_H_ */
