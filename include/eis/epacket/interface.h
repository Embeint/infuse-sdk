/**
 * @file
 * @brief ePacket Interface API
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 * API for the ePacket interfaces.
 */

#ifndef EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_H_
#define EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_H_

#include <zephyr/device.h>
#include <zephyr/net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket interface API
 * @defgroup epacket_interface_apis ePacket interface APIs
 * @{
 */

/* Identifier for ePacket interface */
enum epacket_interface_id {
	EPACKET_INTERFACE_SERIAL = 0,
};

struct epacket_receive_metadata {
	/* ePacket interface packet was received on */
	const struct device *interface;
	/* Numerical ID for interface */
	enum epacket_interface_id interface_id;
	/* RSSI of packet (0 = 0dBm, 20 = 20dBm, etc) */
	int16_t rssi;
};

struct epacket_interface_api {
	void (*packet_overhead)(const struct device *dev, size_t *header, size_t *footer);
	int (*send)(const struct device *dev, struct net_buf *buf);
};

/**
 * @brief Get the packet overhead for an interface
 *
 * @param dev Interface to query
 * @param header Bytes required at start of payload
 * @param footer Bytes required at end of payload
 */
static inline void epacket_packet_overhead(const struct device *dev, size_t *header, size_t *footer)
{
	const struct epacket_interface_api *api = dev->api;

	api->packet_overhead(dev, header, footer);
}

/**
 * @brief Send an ePacket over an interface
 *
 * @param dev Interface to send packet on
 * @param buf Packet to send
 *
 * @retval 0 on success
 * @retval -errno negative error code on failure
 */
static inline int epacket_send(const struct device *dev, struct net_buf *buf)
{
	const struct epacket_interface_api *api = dev->api;

	return api->send(dev, buf);
}

/**
 * @brief Handle raw received ePackets from interfaces
 *
 * @param metadata Interface receive metadata
 * @param buf ePacket that was received
 */
void epacket_raw_receive_handler(struct epacket_receive_metadata *metadata, struct net_buf *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_H_ */
