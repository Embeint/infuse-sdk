/**
 * @file
 * @brief ePacket Bluetooth GATT central
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/toolchain.h>
#include <zephyr/bluetooth/conn.h>

#include <infuse/epacket/interface/common.h>
#include <infuse/epacket/interface/epacket_bt.h>

#ifdef __cplusplus
extern "C" {

#endif

/**
 * @brief epacket_bt_central API
 * @defgroup epacket_bt_central_apis epacket_bt_central APIs
 * @{
 */

#define epacket_bt_central_frame epacket_v0_versioned_frame_format

/**
 * @brief Connect to a peer Infuse-IoT device via Bluetooth GATT
 *
 * @note If called multiple times on the same connection, the subscribe requests
 *       are updated on each call.
 *
 * @param peer Peer device to connect to
 * @param conn_params Connection parameters to setup connection with
 * @param timeout_ms Duration to wait while attempting to setup connection
 * @param conn Output connection object on success
 * @param security Output security parameters of the peer device on success
 * @param subscribe_commands Subscribe to the command characteristic
 * @param subscribe_data Subscribe to the data characteristic
 * @param subscribe_logging Subscribe to the logging characteristic
 *
 * @retval 0 on success, *conn is valid
 * @retval 1 if connection already existed, *conn is valid
 * @retval -errno on error, *conn is invalid
 */
int epacket_bt_gatt_connect(const bt_addr_le_t *peer, const struct bt_le_conn_param *conn_params,
			    uint32_t timeout_ms, struct bt_conn **conn,
			    struct epacket_read_response *security, bool subscribe_commands,
			    bool subscribe_data, bool subscribe_logging);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_ */
