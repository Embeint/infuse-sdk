/**
 * @file
 * @brief ePacket serial packet format
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_serial API
 * @defgroup epacket_serial_apis epacket_serial APIs
 * @{
 */

/* ePacket serial data frame header */
struct epacket_serial_frame_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

/* ePacket serial data frame */
struct epacket_serial_frame {
	/* AEAD associated data */
	union {
		struct {
			/* Frame version */
			uint8_t version;
			/* Payload type */
			uint8_t type;
			/* Payload flags */
			uint16_t flags;
			/* Encryption metadata */
			union {
				/* Network key identifier */
				uint8_t network_id[3];
				/* Device key rotation */
				uint8_t device_rotation[3];
			};
			/* Infuse device ID (upper 4 bytes) */
			uint32_t device_id_upper;
		} __packed;
		uint8_t raw[11];
	} associated_data;
	/* AEAD encryption nonce (IV) */
	union {
		struct {
			/* Infuse device ID (lower 4 bytes) */
			uint32_t device_id_lower;
			/* Local GPS time (seconds) */
			uint32_t gps_time;
			/* Packet sequence number */
			uint16_t sequence;
			/* Random entropy */
			uint16_t entropy;
		} __packed;
		uint8_t raw[12];
	} nonce;
	/* Ciphertext + tag bytes */
	uint8_t ciphertext_tag[];
} __packed;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_ */
