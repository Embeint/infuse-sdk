/**
 * @file
 * @brief ePacket dummy packet format
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_DUMMY_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_DUMMY_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_dummy API
 * @defgroup epacket_dummy_apis epacket_dummy APIs
 * @{
 */

/* Expected size of the dummy frame header */
#define EPACKET_DUMMY_FRAME_EXPECTED_SIZE 3

/* ePacket dummy data frame */
struct epacket_dummy_frame {
	/* Payload type */
	uint8_t type;
	/* Payload auth */
	uint8_t auth;
	/* Payload flags */
	uint16_t flags;
	/* Payload bytes */
	uint8_t payload[];
} __packed;

/**
 * @brief Get the FIFO that the dummy interface "sends" packets on
 *
 * @return Pointer to the transmit FIFO
 */
struct k_fifo *epacket_dummmy_transmit_fifo_get(void);

/**
 * @brief If set to a non-zero value, treat all sends as errors
 *
 * @param error_code Error code to return.
 */
void epacket_dummy_set_tx_failure(int error_code);

/**
 * @brief Simulate the dummy interface receiving a packet
 *
 * @param dev Dummy interface
 * @param header Packet header
 * @param payload Packet payload
 * @param payload_len Length of payload
 */
void epacket_dummy_receive(const struct device *dev, const struct epacket_dummy_frame *header, const void *payload,
			   size_t payload_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_DUMMY_H_ */
