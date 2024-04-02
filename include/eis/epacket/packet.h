/**
 * @file
 * @brief ePacket packet APIs
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Structures for ePacket packets
 */

#ifndef EMBEINT_SDK_INCLUDE_EIS_EPACKET_PACKET_H_
#define EMBEINT_SDK_INCLUDE_EIS_EPACKET_PACKET_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket packet API
 * @defgroup epacket_packet_apis ePacket packet APIs
 * @{
 */

enum epacket_auth {
	EPACKET_AUTH_NETWORK,
	EPACKET_AUTH_DEVICE
} __packed;

struct epacket_metadata {
	enum epacket_auth auth;
};

/**
 * @brief Allocate ePacket TX buffer
 *
 * @param timeout Maximum duration to wait for buffer
 * @retval NULL On timeout
 * @retval buf When successfully allocated
 */
struct net_buf *epacket_alloc_tx(k_timeout_t timeout);

/**
 * @brief Allocate ePacket RX buffer
 *
 * @param timeout Maximum duration to wait for buffer
 * @retval NULL On timeout
 * @retval buf When successfully allocated
 */
struct net_buf *epacket_alloc_rx(k_timeout_t timeout);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_PACKET_H_ */
