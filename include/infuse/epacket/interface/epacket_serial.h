/**
 * @file
 * @brief ePacket serial packet format
 * @copyright 2024 Embeint Inc
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
 * @brief epacket_serial API
 * @defgroup epacket_serial_apis epacket_serial APIs
 * @{
 */

#define EPACKET_SERIAL_SYNC_A 0xD5
#define EPACKET_SERIAL_SYNC_B 0xCA

/* ePacket serial data frame header */
struct epacket_serial_frame_header {
	uint8_t sync[2];
	uint16_t len;
} __packed;

#define epacket_serial_frame epacket_v0_versioned_frame_format

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_SERIAL_H_ */
