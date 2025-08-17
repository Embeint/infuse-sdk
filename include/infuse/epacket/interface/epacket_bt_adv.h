/**
 * @file
 * @brief ePacket Bluetooth advertising packet format
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_ADV_H_
#define INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_ADV_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include <infuse/epacket/interface/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief epacket_bt_adv API
 * @defgroup epacket_bt_adv_apis epacket_bt_adv APIs
 * @{
 */

#define epacket_bt_adv_frame epacket_v0_versioned_frame_format

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_ADV_H_ */
