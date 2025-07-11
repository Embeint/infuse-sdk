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
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>

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
	/* Packet failed to decrypt */
	EPACKET_AUTH_FAILURE = 0,
	/* Packet is encrypted for remote device */
	EPACKET_AUTH_REMOTE_ENCRYPTED = 0,
	EPACKET_AUTH_NETWORK = 1,
	EPACKET_AUTH_DEVICE = 2,
} __packed;

/* Interface specific addresses */
union epacket_interface_address {
	/* Bluetooth LE address */
	bt_addr_le_t bluetooth;
};

/* Empty interface address */
#define EPACKET_ADDR_ALL                                                                           \
	(union epacket_interface_address)                                                          \
	{                                                                                          \
	}

/**
 * @brief Callback run when packet is transmitted
 *
 * @param dev Interface packet was sent on
 * @param pkt Packet that was sent
 * @param result Result of sending the packet
 */
typedef void (*epacket_tx_done_cb)(const struct device *dev, struct net_buf *pkt, int result);

/* Metadata for packets that will be transmitted */
struct epacket_tx_metadata {
	/* Callback run when TX completes */
	epacket_tx_done_cb tx_done;
	/* Authentication level of packet */
	enum epacket_auth auth;
	/* Packet type */
	enum infuse_type type;
	/* Flags to apply to packet */
	uint16_t flags;
	/* Sequence number used for packet */
	uint16_t sequence;
	/* Interface specific address */
	union epacket_interface_address interface_address;
};

/* Metadata for packets that have been received */
struct epacket_rx_metadata {
	/* Device ID in packet */
	uint64_t packet_device_id;
	/* GPS time in packet */
	uint32_t packet_gps_time;
	/* Key ID used by packet */
	uint32_t key_identifier;
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
	/* Interface specific address */
	union epacket_interface_address interface_address;
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
	/* Bit 14: Transmitting device requests an ACK */
	EPACKET_FLAGS_ACK_REQUEST = BIT(14),
	/* Bits 8-13: Reserved */
	/* Bits 0-7: Interface specific */
	EPACKET_FLAGS_INTERFACE_MASK = 0x00FF,
};

/** If a single byte payload with this value is received on an interface,
 * respond with a @ref INFUSE_KEY_IDS packet.
 */
#define EPACKET_KEY_ID_REQ_MAGIC 0x4D

/** Format of @ref INFUSE_KEY_IDS packet */
struct epacket_key_ids_data {
	uint8_t device_key_id[3];
};

/** Format of BLE address in @ref INFUSE_RECEIVED_EPACKET and @ref INFUSE_EPACKET_FORWARD */
struct epacket_interface_address_bt_le {
	uint8_t type;
	uint8_t addr[6];
} __packed;
BUILD_ASSERT(sizeof(struct epacket_interface_address_bt_le) == 7);

/** Common header for @ref INFUSE_RECEIVED_EPACKET */
struct epacket_received_common_header {
	/*    Bit 16: 1 when packet is still encrypted
	 *            0 when packet is decrypted
	 * Bits 0-15: Total length of headers + data
	 */
	uint16_t len_encrypted;
	/* Received packet signal strength (0 - val) */
	uint8_t rssi;
	/* Value from `EPACKET_INTERFACE_*` */
	uint8_t interface;
} __packed;

/** Header for @ref INFUSE_RECEIVED_EPACKET where packet was decrypted */
struct epacket_received_decrypted_header {
	/* Device ID in the packet */
	uint64_t device_id;
	/* GPS time in the packet */
	uint32_t gps_time;
	/* Packet type */
	uint8_t type;
	/* Packet flags */
	uint16_t flags;
	/* Sequence number */
	uint16_t sequence;
	/* ID associated with the key */
	uint8_t key_id[3];
} __packed;

/** Common header for @ref INFUSE_EPACKET_FORWARD */
struct epacket_forward_header {
	/* Total length of this header + payload */
	uint16_t length;
	/* Value from `EPACKET_INTERFACE_*` */
	uint8_t interface;
	/* Destination interface address + packet bytes */
	uint8_t destination_payload[];
} __packed;

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
	uint16_t size;

	if (buf == NULL) {
		return NULL;
	}
	/* Reserve space for header */
	net_buf_reserve(buf, config->header_size);
	/* Limit size based on interface */
	size = epacket_interface_max_packet_size(dev);
	if (size > (config->header_size + config->footer_size)) {
		/* Hacky reservation for footer, automatically reversed by epacket_queue */
		buf->size = size - config->footer_size;
	} else {
		/* 0 payload size */
		buf->size = config->header_size;
	}
	return buf;
}

/**
 * @brief Set metadata on a packet
 *
 * @param buf ePacket TX buffer
 * @param auth Authentication level to use for packet
 * @param flags Desired packet flags
 * @param type Packet type
 * @param dest Destination address
 */
static inline void epacket_set_tx_metadata(struct net_buf *buf, enum epacket_auth auth,
					   uint16_t flags, enum infuse_type type,
					   union epacket_interface_address dest)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	meta->auth = auth;
	meta->flags = flags;
	meta->type = type;
	meta->tx_done = NULL;
	meta->interface_address = dest;
}

/**
 * @brief Set callback to be run after packet sent
 *
 * @param buf ePacket TX buffer
 * @param tx_done Callback to run on transmission
 */
static inline void epacket_set_tx_callback(struct net_buf *buf, epacket_tx_done_cb tx_done)
{
	struct epacket_tx_metadata *meta = net_buf_user_data(buf);

	meta->tx_done = tx_done;
}

/**
 * @brief Append received packet to storage buffer
 *
 * @param storage_buf Buffer of type @ref INFUSE_RECEIVED_EPACKET
 * @param received_buf Receive ePacket to append to @a storage_buf
 *
 * @retval 0 on success
 * @retval -ENOMEM if insufficient space exists on @a storage_buf
 */
int epacket_received_packet_append(struct net_buf *storage_buf, struct net_buf *received_buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_PACKET_H_ */
