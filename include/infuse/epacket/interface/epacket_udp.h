/**
 * @file
 * @brief ePacket UDP packet format
 * @copyright 2024 Embeint Holdings Pty Ltd
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

struct net_if;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ePacket UDP interface
 * @defgroup epacket_udp_apis ePacket UDP interface
 * @ingroup epacket_interface_apis
 * @{
 */

/** UDP interface frame format, @ref epacket_v0_unversioned_frame_format */
#define epacket_udp_frame epacket_v0_unversioned_frame_format

/** UDP specific packet flags */
enum epacket_flags_udp {
	/** Packet is a single transmission on the interface.
	 * This can be used for backends as a hint that the interface
	 * can be dropped back to an idle state (e.g LTE RAI).
	 * If this flag is set and additional packets are queued, there
	 * may be power, time and efficiency implications.
	 */
	EPACKET_FLAGS_UDP_SINGLE_TX = BIT(1),
	/** Device is always available to receive packets */
	EPACKET_FLAGS_UDP_ALWAYS_RX = BIT(0),
};

/**
 * @brief Set flags for the UDP interface
 *
 * @param flags Bitmask of @ref epacket_flags_udp flags
 */
void epacket_udp_flags_set(uint16_t flags);

/**
 * @brief Cycle a network interface without resetting the downlink watchdog
 *
 * Takes the interface administratively down, back up, and then requests a connection.
 * Intended for recovering the underlying hardware (e.g. forcing a SIM card to be
 * re-evaluated), where the application still wants connectivity throughout.
 * Unlike calling @ref net_if_down and @ref net_if_up directly, the downlink watchdog
 * (@kconfig{CONFIG_EPACKET_INTERFACE_UDP_DOWNLINK_WATCHDOG}) is not cancelled or restarted.
 *
 * @note Admin state changes on non-persistent interfaces (no @ref CONN_MGR_IF_PERSISTENT flag)
 *       already leave a running watchdog untouched, so this only changes the watchdog
 *       behaviour for persistent interfaces and interfaces without a connectivity binding.
 *
 * @warning Not thread-safe, concurrent cycles (of the same or different interfaces) are not
 *          supported.
 *
 * @param iface Network interface to cycle
 *
 * @retval 0 on success
 * @retval -ENETDOWN if the interface is not administratively up
 * @retval -errno error code from @ref net_if_down, @ref net_if_up or @ref conn_mgr_if_connect
 */
int epacket_udp_interface_cycle(struct net_if *iface);

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
