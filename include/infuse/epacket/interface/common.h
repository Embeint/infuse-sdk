/**
 * @file
 * @brief Common packet structures
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_COMMON_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_COMMON_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket interface common API
 * @defgroup epacket_interface_common_apis ePacket interface common APIs
 * @{
 */

/* Versioned data frame */
struct epacket_v0_versioned_frame_format {
	/* AEAD associated data */
	union {
		struct {
			/* Frame version */
			uint8_t version;
			/* Payload type */
			uint8_t type;
			/* Payload flags */
			uint16_t flags;
			/* Network or device key identifier */
			uint8_t key_identifier[3];
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

/* Unversioned data frame */
struct epacket_v0_unversioned_frame_format {
	/* AEAD associated data */
	union {
		struct {
			/* Payload type */
			uint8_t type;
			/* Payload flags */
			uint16_t flags;
			/* Network or device key identifier */
			uint8_t key_identifier[3];
			/* Infuse device ID (upper 4 bytes) */
			uint32_t device_id_upper;
		} __packed;
		uint8_t raw[10];
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

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_COMMON_H_ */
