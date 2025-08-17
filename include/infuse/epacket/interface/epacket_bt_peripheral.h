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
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_PERIPHERAL_H_ */
