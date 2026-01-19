/**
 * @file
 * @brief ePacket dummy packet format
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
#define EPACKET_DUMMY_FRAME_EXPECTED_SIZE 8

/* ePacket dummy data frame */
struct epacket_dummy_frame {
	/* Payload type */
	uint8_t type;
	/* Payload auth */
	uint8_t auth;
	/* Payload flags */
	uint16_t flags;
	/* Key identifier */
	uint32_t key_identifier;
	/* Payload bytes */
	uint8_t payload[];
} __packed;

/**
 * @brief Reset registered epacket callbacks
 *
 * @param dev Dummy interface
 */
void epacket_dummy_reset_callbacks(const struct device *dev);

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
 * @brief Set the maximum packet size for the interface
 *
 * @param packet_size New maximum packet size
 */
void epacket_dummy_set_max_packet(uint16_t packet_size);

/**
 * @brief Set the interface state and run callbacks
 *
 * @param dev Dummy interface
 * @param state New interface state
 */
void epacket_dummy_set_interface_state(const struct device *dev, bool state);

/**
 * @brief Is receiving currently scheduled on the interface
 *
 * @retval true Receiving has been enabled
 * @retval false Receiving has been disabled
 */
bool epacket_dummy_receive_scheduled(void);

/**
 * @brief Override the behaviour of `.receive_ctrl`
 *
 * @param func_exists True to populate function in API struct, false to remove
 * @param rc Return code of `.receive_ctrl`
 */
void epacket_dummy_receive_api_override(bool func_exists, int rc);

/**
 * @brief Simulate the dummy interface receiving a packet
 *
 * @param dev Dummy interface
 * @param header Packet header
 * @param payload Packet payload
 * @param payload_len Length of payload
 * @param extra Extra payload
 * @param extra_len Length of extra payload
 */
void epacket_dummy_receive_extra(const struct device *dev, const struct epacket_dummy_frame *header,
				 const void *payload, size_t payload_len, const void *extra,
				 size_t extra_len);

/**
 * @brief Simulate the dummy interface receiving a packet
 *
 * @param dev Dummy interface
 * @param header Packet header
 * @param payload Packet payload
 * @param payload_len Length of payload
 */
static inline void epacket_dummy_receive(const struct device *dev,
					 const struct epacket_dummy_frame *header,
					 const void *payload, size_t payload_len)
{
	epacket_dummy_receive_extra(dev, header, payload, payload_len, NULL, 0);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_DUMMY_H_ */
