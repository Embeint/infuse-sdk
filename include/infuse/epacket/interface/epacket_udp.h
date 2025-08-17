/**
 * @file
 * @brief ePacket UDP packet format
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_

#include <stdint.h>

#include <zephyr/toolchain.h>
#include <zephyr/sys/util.h>

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

/** UDP specific packet flags */
enum epacket_flags_udp {
	/** Device is always available to receive packets */
	EPACKET_FLAGS_UDP_ALWAYS_RX = BIT(0),
};

/**
 * @brief Set flags for the UDP interface
 *
 * @param flags Bitmask of @ref epacket_flags_udp flags
 */
void epacket_udp_flags_set(uint16_t flags);

#ifdef CONFIG_ZTEST

/**
 * @brief Reset DNS knowledge for testing purposes
 */
void epacket_udp_dns_reset(void);

#endif /* CONFIG_ZTEST */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_UDP_H_ */
