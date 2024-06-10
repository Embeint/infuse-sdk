/**
 * @file
 * @brief ePacket packet APIs
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * Structures for ePacket packets
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_PACKET_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_PACKET_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/toolchain.h>
#include <zephyr/net/buf.h>

#include <infuse/types.h>
#include <infuse/epacket/interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket packet API
 * @defgroup epacket_packet_apis ePacket packet APIs
 * @{
 */

enum epacket_auth {
	EPACKET_AUTH_FAILURE,
	EPACKET_AUTH_NETWORK,
	EPACKET_AUTH_DEVICE,
} __packed;

struct epacket_tx_metadata {
	void (*tx_done)(const struct device *dev, struct net_buf *pkt, int result);
	enum epacket_auth auth;
	enum infuse_type type;
	uint16_t flags;
};

struct epacket_rx_metadata {
	/* Authentication level of packet */
	enum epacket_auth auth;
	/* Type of packet */
	enum infuse_type type;
	/* Flags associated with packet */
	uint16_t flags;
	/* ePacket interface packet was received on */
	const struct device *interface;
	/* Numerical ID for interface */
	enum epacket_interface_id interface_id;
	/* RSSI of packet (0 = 0dBm, 20 = 20dBm, etc) */
	int16_t rssi;
	/* Sequence number of packet */
	uint16_t sequence;
};

/* Global ePacket flags */
enum epacket_flags {
	/* Bit 15: Encryption Type */
	EPACKET_FLAGS_ENCRYPTION_DEVICE = BIT(15),
	EPACKET_FLAGS_ENCRYPTION_NETWORK = 0,
	/* Bits 8-14: Reserved */
	/* Bits 0-7: Interface specific */
	EPACKET_FLAGS_INTERFACE_MASK = 0x00FF,
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
static inline struct net_buf *epacket_alloc_tx_for_interface(const struct device *dev,
							     k_timeout_t timeout)
{
	const struct epacket_interface_common_config *config = dev->config;
	struct net_buf *buf = epacket_alloc_tx(timeout);

	if (buf == NULL) {
		return NULL;
	}
	/* Reserve space for header */
	net_buf_reserve(buf, config->header_size);
	/* Hacky reservation for footer, automatically reversed by epacket_queue */
	buf->size -= config->footer_size;
	return buf;
}

/**
 * @brief Set metadata on a packet
 *
 * @param buf ePacket TX buffer
 * @param auth Authentication level to use for packet
 * @param flags Desired packet flags
 * @param type Packet type
 */
static inline void epacket_set_tx_metadata(struct net_buf *buf, enum epacket_auth auth,
					   uint16_t flags, enum infuse_type type)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	meta->auth = auth;
	meta->flags = flags;
	meta->type = type;
	meta->tx_done = NULL;
}

/**
 * @brief Set callback to be run after packet sent
 *
 * @param buf ePacket TX buffer
 * @param tx_done Callback to run on transmission
 */
static inline void epacket_set_tx_callback(struct net_buf *buf,
					   void (*tx_done)(const struct device *dev,
							   struct net_buf *pkt, int result))
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	meta->tx_done = tx_done;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_PACKET_H_ */
