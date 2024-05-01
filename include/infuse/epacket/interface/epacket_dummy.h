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

struct k_fifo *epacket_dummmy_transmit_fifo_get(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_DUMMY_H_ */
