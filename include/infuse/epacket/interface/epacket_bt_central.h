/**
 * @file
 * @brief ePacket Bluetooth GATT central
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/toolchain.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

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

/** @brief Parameters for @ref epacket_bt_gatt_connect */
struct epacket_bt_gatt_connect_params {
	/* Connection parameters to setup connection with */
	struct bt_le_conn_param conn_params;
	/* Peer device to connect to */
	bt_addr_le_t peer;
	/* Automatically disconnect if no data sent or received on the command or data
	 * characteristics for this long. K_FOREVER to disable.
	 */
	k_timeout_t inactivity_timeout;
	/* Unconditionally terminate the connection after this long. K_FOREVER to disable. */
	k_timeout_t absolute_timeout;
	/* Duration to wait while attempting to setup connection */
	uint32_t conn_timeout_ms;
	/* Preferred PHY of the connection (BT_GAP_LE_PHY_*) */
	uint8_t preferred_phy;
	/* Subscribe to the command characteristic */
	bool subscribe_commands;
	/* Subscribe to the data characteristic */
	bool subscribe_data;
	/* Subscribe to the logging characteristic */
	bool subscribe_logging;
};

/**
 * @brief Connect to a peer Infuse-IoT device via Bluetooth GATT
 *
 * @note If called multiple times on the same connection, the subscribe requests,
 *       inactivity and absolute timeout are updated on each call.
 *
 * @param conn Output connection object on success
 * @param params ePacket connection parameters
 * @param security Output security parameters of the peer device on success
 *
 * @retval 0 on success, *conn is valid
 * @retval >0 on HCI error, *conn is invalid
 * @retval <0 on Zephyr error, *conn is invalid
 */
int epacket_bt_gatt_connect(struct bt_conn **conn, struct epacket_bt_gatt_connect_params *params,
			    struct epacket_read_response *security);

/**
 * @brief Infuse-IoT Bluetooth GATT characteristic notification handle function
 *
 * Public API function so that connections setup through a function other than
 * @ref epacket_bt_gatt_connect can hook the connection up as an ePacket data source
 * dynamically.
 *
 * @param conn Connection object. May be NULL, indicating that the peer is
 *             being unpaired
 * @param params Subscription parameters.
 * @param data Attribute value data. If NULL then subscription was removed.
 * @param length Attribute value length.
 *
 * @return BT_GATT_ITER_CONTINUE to continue receiving value notifications.
 *         BT_GATT_ITER_STOP to unsubscribe from value notifications.
 */
uint8_t epacket_bt_gatt_notify_recv_func(struct bt_conn *conn,
					 struct bt_gatt_subscribe_params *params, const void *data,
					 uint16_t length);

/**
 * @brief Send a @ref epacket_rate_limit_req to all connected peer devices
 *
 * @param delay_ms Delay duration to request
 */
void epacket_bt_gatt_rate_limit_request(uint8_t delay_ms);

/**
 * @brief Send a @ref epacket_rate_throughput_req to a specific peer device
 *
 * @param conn Connection object
 * @param throughput_kbps Requested throughput limit in kilobits/sec
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int epacket_bt_gatt_rate_throughput_request(struct bt_conn *conn, uint16_t throughput_kbps);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_CENTRAL_H_ */
