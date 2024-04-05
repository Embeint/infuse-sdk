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
#include <zephyr/net/buf.h>

#include <eis/epacket/interface.h>

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
	uint16_t flags;
	uint8_t type;
};

/**
 * @brief Allocate ePacket TX buffer
 *
 * @warning This function does not reserve space on the
 *          buffer for packet headers and footers.
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
 * @brief Allocate ePacket TX buffer for a specific interface
 *
 * @param dev ePacket interface to
 * @param timeout Maximum duration to wait for buffer
 * @retval NULL On timeout
 * @retval buf When successfully allocated
 */
static inline struct net_buf *epacket_alloc_tx_for_interface(const struct device *dev, k_timeout_t timeout)
{
	struct net_buf *buf = epacket_alloc_tx(timeout);
	size_t header, footer;

	if (buf == NULL) {
		return NULL;
	}
	/* Query interface overheads */
	epacket_packet_overhead(dev, &header, &footer);
	/* Reserve space for header */
	net_buf_reserve(buf, header);
	return buf;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_PACKET_H_ */
