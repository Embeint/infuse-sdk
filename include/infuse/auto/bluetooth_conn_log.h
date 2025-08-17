/**
 * @file
 * @brief Automatically log Bluetooth connection events
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_BLUETOOTH_CONN_LOG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_BLUETOOTH_CONN_LOG_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief bluetooth_conn_log API
 * @defgroup bluetooth_conn_log_apis bluetooth_conn_log APIs
 * @{
 */

enum {
	/* Automatically flush the logger when logging events */
	AUTO_BT_CONN_LOG_EVENTS_FLUSH = BIT(7),
};

/**
 * @brief Automatically log Bluetooth connection events
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 * @param flags Extra `AUTO_BT_CONN_LOG_` flags
 */
void auto_bluetooth_conn_log_configure(uint8_t tdf_logger_mask, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_BLUETOOTH_CONN_LOG_H_ */
