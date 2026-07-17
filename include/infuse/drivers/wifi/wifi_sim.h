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
#include <stddef.h>

#include <zephyr/net/wifi_mgmt.h>

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
 * @brief Configure scan results returned by the simulated device
 *
 * The provided array must remain valid until this function is called again with
 * a different array or a zero result count. Passing NULL or a zero count clears
 * the simulated scan result list.
 *
 * This function should not be called while a scan is ongoing, between @a NET_REQUEST_WIFI_SCAN
 * and the final callback.
 *
 * @param results Array of scan results to return
 * @param result_count Number of entries in @p results
 */
void wifi_sim_scan_results_set(const struct wifi_scan_result *results, size_t result_count);

/**
 * @brief Get the latest scan parameters provided to the simulated device
 *
 * The returned object is owned by the simulated driver and is updated whenever
 * @a NET_REQUEST_WIFI_SCAN is handled.
 *
 * @return Latest scan parameters object
 */
const struct wifi_scan_params *wifi_sim_scan_params_get(void);

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
