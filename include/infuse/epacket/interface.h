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

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_

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

/* Maximum packet size on an interface, limited by CONFIG_EPACKET_PACKET_SIZE_MAX */
#define EPACKET_INTERFACE_MAX_PACKET(node_id)                                                                          \
	(MIN(CONFIG_EPACKET_PACKET_SIZE_MAX, DT_PROP_OR(node_id, max_packet_size, CONFIG_EPACKET_PACKET_SIZE_MAX)))
/* Get the maximum payload size for a given packet size */
#define EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, packet_size)                                                    \
	(MIN(packet_size, CONFIG_EPACKET_PACKET_SIZE_MAX) - DT_PROP(node_id, header_size) -                            \
	 DT_PROP(node_id, footer_size))
/* Maximum payload size on an interface */
#define EPACKET_INTERFACE_MAX_PAYLOAD(node_id)                                                                         \
	(EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, EPACKET_INTERFACE_MAX_PACKET(node_id)))

/* Identifier for ePacket interface */
enum epacket_interface_id {
	EPACKET_INTERFACE_SERIAL = 0,
	EPACKET_INTERFACE_UDP = 1,
	EPACKET_INTERFACE_DUMMY = 255,
};

struct epacket_interface_api {
	/**
	 * @brief Send a packet over the interface
	 *
	 * @param dev Interface device
	 * @param buf Packet to send
	 *
	 * @retval 0 on success
	 * @retval -ENOTCONN if interface not connected
	 * @retval -errno on other error
	 */
	int (*send)(const struct device *dev, struct net_buf *buf);
};

/**
 * @brief Callback to run on received packet
 *
 * @param packet Received packet payload
 */
typedef void (*epacket_receive_handler)(struct net_buf *packet);

/** Common data struct for all interfaces. Must be first member in interface data struct */
struct epacket_interface_common_data {
	epacket_receive_handler receive_handler;
};

/** Common config struct for all interfaces. Must be first member in interface config struct */
struct epacket_interface_common_config {
	uint8_t header_size;
	uint8_t footer_size;
};

/**
 * @brief Queue an ePacket for sending over an interface
 *
 * @param dev Interface to send packet on
 * @param buf Packet to send
 */
void epacket_queue(const struct device *dev, struct net_buf *buf);

/**
 * @brief Set the ePacket receive handler for an interface
 *
 * @param dev Interface to set handler for
 * @param handler Handler function to run on received packets
 */
static inline void epacket_set_receive_handler(const struct device *dev, epacket_receive_handler handler)
{
	struct epacket_interface_common_data *data = dev->data;

	data->receive_handler = handler;
}

/**
 * @brief Default ePacket receive handler
 *
 * Currently only prints received packets.
 *
 * @param buf ePacket that was received
 */
void epacket_default_receive_handler(struct net_buf *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_ */
