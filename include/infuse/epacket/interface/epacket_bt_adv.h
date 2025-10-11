/**
 * @file
 * @brief ePacket Bluetooth advertising packet format
 * @copyright 2024 Embeint Holdings Pty Ltd
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
 * @brief Request Bluetooth scanning to be suspended
 *
 * After calling, any active Bluetooth scanning is suspended and any future
 * scanning is disabled until @ref epacket_bt_adv_scan_resume is called.
 *
 * A call to @ref epacket_receive received between these two function calls will
 * be actioned once @ref epacket_bt_adv_scan_resume is called for the remaining
 * duration.
 *
 * The duration of any suspension should be tightly bounded to much less than
 * CONFIG_EPACKET_INTERFACE_BT_ADV_SCAN_WATCHDOG_SEC (default 10 minutes).
 *
 * This can be useful when performing actions where performance is typically degraded
 * by Bluetooth activity, for example writing to internal flash.
 */
void epacket_bt_adv_scan_suspend(void);

/**
 * @brief Release a request for Bluetooth scanning to be suspended
 *
 * Release the constraint of Bluetooth scanning created by @ref epacket_bt_adv_scan_suspend.
 */
void epacket_bt_adv_scan_resume(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_EPACKET_INTERFACE_EPACKET_BT_ADV_H_ */
