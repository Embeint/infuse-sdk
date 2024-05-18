/**
 * @file
 * @brief ePacket Interface API
 * @copyright 2024 Embeint Inc
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
#define EPACKET_INTERFACE_MAX_PACKET(node_id)                                                      \
	(MIN(CONFIG_EPACKET_PACKET_SIZE_MAX,                                                       \
	     DT_PROP_OR(node_id, max_packet_size, CONFIG_EPACKET_PACKET_SIZE_MAX)))
/* Get the maximum payload size for a given packet size */
#define EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, packet_size)                                \
	(MIN(packet_size, CONFIG_EPACKET_PACKET_SIZE_MAX) - DT_PROP(node_id, header_size) -        \
	 DT_PROP(node_id, footer_size))
/* Maximum payload size on an interface */
#define EPACKET_INTERFACE_MAX_PAYLOAD(node_id)                                                     \
	(EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, EPACKET_INTERFACE_MAX_PACKET(node_id)))

/* Identifier for ePacket interface */
enum epacket_interface_id {
	EPACKET_INTERFACE_SERIAL = 0,
	EPACKET_INTERFACE_UDP = 1,
	EPACKET_INTERFACE_DUMMY = 255,
};

/** @brief ePacket interface callback structure. */
struct epacket_interface_cb {
	/**
	 * @brief The interface connection state has changed.
	 *
	 * @param connected True when the interface is connected
	 * @param current_max_payload Current maximum payload size
	 * @param user_ctx User context pointer
	 */
	void (*interface_state)(bool connected, uint16_t current_max_payload, void *user_ctx);

	/**
	 * @brief A packet failed to transmit on the interface
	 *
	 * @param buf The packet that failed to transmit
	 * @param reason The error code from the interface
	 * @param user_ctx User context pointer
	 */
	void (*tx_failure)(const struct net_buf *buf, int reason, void *user_ctx);

	/* User provided context pointer */
	void *user_ctx;

	sys_snode_t node;
};

struct epacket_interface_api {
	/**
	 * @brief Send a packet over the interface
	 *
	 * @note Transmission errors can be detected through @ref epacket_interface_cb
	 *
	 * @param dev Interface device
	 * @param buf Packet to send
	 */
	void (*send)(const struct device *dev, struct net_buf *buf);
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
	sys_slist_t callback_list;
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
static inline void epacket_set_receive_handler(const struct device *dev,
					       epacket_receive_handler handler)
{
	struct epacket_interface_common_data *data = dev->data;

	data->receive_handler = handler;
}

/**
 * @brief Register to be notified of interface events
 *
 * @param dev Interface to receive callbacks for
 * @param cb Callback struct to register
 */
static inline void epacket_register_callback(const struct device *dev,
					     struct epacket_interface_cb *cb)
{
	struct epacket_interface_common_data *data = dev->data;

	sys_slist_append(&data->callback_list, &cb->node);
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
