/**
 * @file
 * @brief ePacket serial packet format
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/epacket/interface/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket serial interface
 * @defgroup epacket_serial_apis ePacket serial interface
 * @ingroup epacket_interface_apis
 * @{
 */

/** First synchronisation byte of a serial frame */
#define EPACKET_SERIAL_SYNC_A 0xD5
/** Second synchronisation byte of a serial frame */
#define EPACKET_SERIAL_SYNC_B 0xCA

/** @brief ePacket serial data frame header */
struct epacket_serial_frame_header {
	/** @ref EPACKET_SERIAL_SYNC_A, @ref EPACKET_SERIAL_SYNC_B */
	uint8_t sync[2];
	/** Length of the frame following this header */
	uint16_t len;
} __packed;

/** Serial interface frame format, @ref epacket_v0_versioned_frame_format */
#define epacket_serial_frame epacket_v0_versioned_frame_format

/**
 * @brief Reset the internal state of @a epacket_serial_reconstruct
 *
 * @param dev Serial device
 */
void epacket_serial_reconstruct_reset(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_ */
