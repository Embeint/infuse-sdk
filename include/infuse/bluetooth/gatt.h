/**
 * @file
 * @brief Infuse-IoT GATT helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_

#include <zephyr/bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup infuse_gatt_apis Infuse-IoT GATT APIs
 * @{
 */

/**
 * @brief Get last measured RSSI on a connection
 *
 * @param conn Bluetooth connection to query
 *
 * @return int8_t Last measured RSSI in dBm (0 before measurement)
 */
int8_t bt_conn_rssi(struct bt_conn *conn);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_GATT_H_ */
