/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_LIB_MODEM_MONITOR_MODEM_MONITOR_H_
#define INFUSE_SDK_LIB_MODEM_MONITOR_MODEM_MONITOR_H_

#include <stdbool.h>

#include <zephyr/net/net_if.h>

/**
 * @brief Initialise the generic monitor
 *
 * @param iface Network interface to monitor
 */
void modem_monitor_init(struct net_if *iface);

/**
 * @brief Notify modem monitor that IP connectivity expected state
 *
 * @param expected True when IP connectivity expected, false otherwise
 */
void modem_monitor_ip_connectivity_expected(bool expected);

#endif /* INFUSE_SDK_LIB_MODEM_MONITOR_MODEM_MONITOR_H_ */
