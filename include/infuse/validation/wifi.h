/**
 * @file
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_WIFI_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_WIFI_H_

#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_wifi Wi-Fi validation API
 * @{
 */

enum {
	/** Validate that Wi-Fi interface powers up */
	VALIDATION_WIFI_POWER_UP = 0,
	/** Validate that Wi-Fi network scan completes */
	VALIDATION_WIFI_SSID_SCAN = BIT(0),
	/** Validate that Wi-Fi connects to defined network */
	VALIDATION_WIFI_CONNECT = BIT(1),
	/** Validate that Wi-Fi can query the current time */
	VALIDATION_WIFI_SNTP_QUERY = BIT(2),
};

/**
 * @brief Validate the behaviour of a Wi-Fi device
 *
 * @param iface Networking interface
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_wifi(struct net_if *iface, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_WIFI_H_ */
