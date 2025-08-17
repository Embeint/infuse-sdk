/**
 * @file
 * @brief Automatically log WiFi connection events
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_WIFI_CONN_LOG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_WIFI_CONN_LOG_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief wifi_conn_log API
 * @defgroup wifi_conn_log_apis wifi_conn_log APIs
 * @{
 */

enum {
	/** Log WiFi network connection successes */
	AUTO_WIFI_LOG_CONNECTION = BIT(0),
	/** Log WiFi network connection failures */
	AUTO_WIFI_LOG_FAILURES = BIT(1),
	/** Log WiFi network disconnects */
	AUTO_WIFI_LOG_DISCONNECTION = BIT(2),
	/** Log all WiFi connection events */
	AUTO_WIFI_LOG_ALL = BIT(0) | BIT(1) | BIT(2),
	/** Automatically flush the logger when logging events */
	AUTO_WIFI_LOG_EVENTS_FLUSH = BIT(7),
};

/**
 * @brief Automatically log WiFi events
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 * @param flags Extra `AUTO_WIFI_LOG_` flags
 */
void auto_wifi_conn_log_configure(uint8_t tdf_logger_mask, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_WIFI_CONN_LOG_H_ */
