/**
 * @file
 * @brief ePacket USB packet format
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_EPACKET_USB_H_
#define EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_EPACKET_USB_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_usb API
 * @defgroup epacket_usb_apis epacket_usb APIs
 * @{
 */

/* Expected size of the USB frame header */
#define EPACKET_USB_FRAME_EXPECTED_SIZE 19

/* ePacket USB data frame */
struct epacket_usb_frame {
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
		} __packed;
		uint8_t raw[7];
	} associated_data;
	/* AEAD encryption nonce (IV) */
	union {
		struct {
			/* EIS unique device ID:
			 *   Transmitting device for network key encryption
			 *   Source/destination device for device key encryption
			 */
			uint32_t device_id;
			/* Local GPS time (seconds) */
			uint32_t gps_time;
			/* Random entropy */
			uint32_t entropy;
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

#endif /* EMBEINT_SDK_INCLUDE_EIS_EPACKET_INTERFACE_EPACKET_USB_H_ */
