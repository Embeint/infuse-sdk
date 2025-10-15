/**
 * @file
 * @brief Simulated Wi-Fi driver
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_WIFI_WIFI_SIM_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_WIFI_WIFI_SIM_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief wifi_sim API
 * @defgroup wifi_sim_apis wifi_sim APIs
 * @{
 */

/**
 * @brief Simulate whether the device is in range of a network
 *
 * @param in_range True when in range, false otherwise
 */
void wifi_sim_in_network_range(bool in_range);

/**
 * @brief If currently connected to a network, disconnect
 */
void wifi_sim_trigger_disconnect(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_WIFI_WIFI_SIM_H_ */
