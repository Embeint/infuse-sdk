/**
 * @file
 * @brief ePacket Interface API
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 * API for the ePacket interfaces.
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_

#include <zephyr/device.h>
#include <zephyr/net_buf.h>

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
/* Overhead of the interface on packet size */
#define EPACKET_INTERFACE_PACKET_OVERHEAD(node_id)                                                 \
	(DT_PROP(node_id, header_size) + DT_PROP(node_id, footer_size))
/* Get the maximum payload size for a given packet size */
#define EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, packet_size)                                \
	(MIN(packet_size, CONFIG_EPACKET_PACKET_SIZE_MAX) -                                        \
	 EPACKET_INTERFACE_PACKET_OVERHEAD(node_id))
/* Maximum payload size on an interface */
#define EPACKET_INTERFACE_MAX_PAYLOAD(node_id)                                                     \
	(EPACKET_INTERFACE_PAYLOAD_FROM_PACKET(node_id, EPACKET_INTERFACE_MAX_PACKET(node_id)))

/**
 * @brief Will this ePacket interface be compiled in?
 *
 * @param node_id ePacket interface node identitier
 *
 * @retval 1 if interface will be compiled in
 * @retval 0 if interface will NOT be compiled in
 */
#define EPACKET_INTERFACE_IS_COMPILED_IN(node_id) IS_ENABLED(DT_STRING_TOKEN(node_id, depends_on))

/* Identifier for ePacket interface */
enum epacket_interface_id {
	EPACKET_INTERFACE_SERIAL = 0,
	EPACKET_INTERFACE_UDP = 1,
	EPACKET_INTERFACE_BT_ADV = 2,
	EPACKET_INTERFACE_BT_PERIPHERAL = 3,
	EPACKET_INTERFACE_BT_CENTRAL = 4,
	EPACKET_INTERFACE_HCI = 10,
	EPACKET_INTERFACE_DUMMY = 255,
};

/** @brief ePacket interface callback structure. */
struct epacket_interface_cb {
	/**
	 * @brief The interface connection state has changed.
	 *
	 * @param current_max_payload Current maximum payload size (0 == disconnected)
	 * @param user_ctx User context pointer
	 */
	void (*interface_state)(uint16_t current_max_payload, void *user_ctx);

	/**
	 * @brief The interface attempted to transmit a packet
	 *
	 * @param buf The packet that was queued for transmission
	 * @param result The result of the transmission (0 == success, negative on error)
	 * @param user_ctx User context pointer
	 */
	void (*tx_result)(const struct net_buf *buf, int result, void *user_ctx);

	/**
	 * @brief A packet was received on the interface
	 *
	 * @note Buffer contents must not be modified from this context if the
	 *       function returns true.
	 * @note This context must never free the provided buffer.
	 *
	 * @param buf The packet that was received
	 * @param decrypted True if packet contents have been decrypted successfully
	 * @param user_ctx User context pointer
	 *
	 * @retval true Continue default processing the packet
	 * @retval false Don't run the default interface packet handler
	 */
	bool (*packet_received)(struct net_buf *buf, bool decrypted, void *user_ctx);

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
	/**
	 * @brief Callback for packet decryption result
	 *
	 * Can be used for downlink tracking in backend drivers.
	 *
	 * @param dev Interface device
	 * @param buf Packet buffer
	 * @param decrypt_result 0 if successfully decrypted, -1 otherwise
	 */
	void (*decrypt_result)(const struct device *dev, struct net_buf *buf, int decrypt_result);
	/**
	 * @brief Control receiving on the interface
	 *
	 * @param dev Interface device
	 * @param enable True to enable receiving, false to disable
	 *
	 * @retval 0 on success
	 * @retval -errno on failure
	 */
	int (*receive_ctrl)(const struct device *dev, bool enable);
	/**
	 * @brief Get current maximum packet size
	 *
	 * If not defined, @a max_packet_size from @ref epacket_interface_common_config is used
	 *
	 * @param dev Interface device
	 *
	 * @returns Maximum packet size
	 */
	uint16_t (*max_packet_size)(const struct device *dev);
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
	struct k_work_delayable receive_timeout;
	struct k_spinlock callback_lock;
	sys_slist_t callback_list;
	const struct device *dev;
};

/** Common config struct for all interfaces. Must be first member in interface config struct */
struct epacket_interface_common_config {
	uint16_t max_packet_size;
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
 * @brief Enable receiving on the interface for a duration
 *
 * @note Each call to this function overrides any previous configured duration.
 *       For example, scheduling a 100 second receive then immediately
 *       scheduling a 10 second receive will result in a 10 second receive
 *       window.
 *
 * @param dev Interface to control receive on
 * @param timeout Duration to receive for.
 *                K_FOREVER = Receive forever
 *                K_NO_WAIT = Stop receiving immediately
 *
 * @retval -ENOTSUP if interface does not support RX control
 * @retval errno if receive control failed
 * @retval 0 if receiving stopped immediately
 * @retval 1 if receive stop has been scheduled for the future
 */
int epacket_receive(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Send a @ref INFUSE_KEY_IDS packet on an interface
 *
 * @param dev Interface to send packet on
 * @param timeout Duration to wait for message buffer
 *
 * @retval 0 If packet was queued
 * @retval -EAGAIN If packet buffer claim timed out
 */
int epacket_send_key_ids(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Get current maximum packet size
 *
 * @param dev Interface to query
 *
 * @retval size Maximum packet size (header + payload + footer)
 */
static inline uint16_t epacket_interface_max_packet_size(const struct device *dev)
{
	const struct epacket_interface_common_config *cfg = dev->config;
	const struct epacket_interface_api *api = dev->api;

	if (api->max_packet_size) {
		return api->max_packet_size(dev);
	} else {
		return cfg->max_packet_size;
	}
}

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

	K_SPINLOCK(&data->callback_lock) {
		sys_slist_append(&data->callback_list, &cb->node);
	}
}

/**
 * @brief Unregister from interface events
 *
 * @param dev Interface to unregister from
 * @param cb Callback struct to unregister
 *
 * @retval true If structure was removed from callback list
 * @retval false If structure was not present on list
 */
static inline bool epacket_unregister_callback(const struct device *dev,
					       struct epacket_interface_cb *cb)
{
	struct epacket_interface_common_data *data = dev->data;
	bool ret = 0;

	K_SPINLOCK(&data->callback_lock) {
		ret = sys_slist_find_and_remove(&data->callback_list, &cb->node);
	}

	return ret;
}

/**
 * @brief Default ePacket receive handler
 *
 * Currently handles echo and RPC requests.
 *
 * @param buf ePacket that was received
 */
void epacket_default_receive_handler(struct net_buf *buf);

/**
 * @brief Default gateway receive handler
 *
 * Received Bluetooth advertising packets are forwarded over backhaul.
 * All other packets handled by @ref epacket_default_receive_handler.
 *
 * @param backhaul ePacket interface to forward packets on.
 * @param buf ePacket that was received
 */
void epacket_gateway_receive_handler(const struct device *backhaul, struct net_buf *buf);

/**
 * @brief Define a handler for a specific backhaul interface
 *
 * @param name Name of handler function
 * @param backhaul Backhaul interface
 */
#define GATEWAY_HANDLER_DEFINE(name, backhaul)                                                     \
	static void name(struct net_buf *buf)                                                      \
	{                                                                                          \
		epacket_gateway_receive_handler(backhaul, buf);                                    \
	}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_H_ */
