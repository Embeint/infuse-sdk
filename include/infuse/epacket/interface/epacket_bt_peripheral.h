/**
 * @file
 * @brief ePacket Bluetooth GATT peripheral packet format
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_PERIPHERAL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_PERIPHERAL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/epacket/interface/common.h>
#include <infuse/epacket/interface/epacket_bt.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_bt_peripheral API
 * @defgroup epacket_bt_peripheral_apis epacket_bt_peripheral APIs
 * @{
 */

#define epacket_bt_peripheral_frame epacket_v0_versioned_frame_format

/**
 * @brief Check whether the CLOUD_UPLINK characteristic has been subscribed
 *
 * @param dev ePacket Bluetooth peripheral device
 * @param conn Connection to check
 *
 * @return true if the attribute object has been subscribed.
 */
bool epacket_bt_peripheral_cloud_uplink_subscribed(const struct device *dev, struct bt_conn *conn);

/**
 * @brief Queue an ePacket on the CLOUD_UPLINK characteristic on a specific connection
 *
 * @param dev ePacket Bluetooth peripheral device
 * @param conn Connection to queue packet on
 * @param buf ePacket to queue
 *
 * @return 0 On success
 * @retval -EIO On encryption error
 * @retval -errno Other error code from @a bt_gatt_notify
 */
int epacket_bt_peripheral_cloud_uplink_send(const struct device *dev, struct bt_conn *conn,
					    struct net_buf *buf);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_PERIPHERAL_H_ */
