/**
 * @file
 * @brief ePacket UDP packet format
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/epacket/interface/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_udp API
 * @defgroup epacket_udp_apis epacket_udp APIs
 * @{
 */

#define epacket_udp_frame epacket_v0_unversioned_frame_format

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_ */
