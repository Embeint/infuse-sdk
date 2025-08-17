/**
 * @file
 * @brief Legacy Bluetooth advertising
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_LEGACY_ADV_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_LEGACY_ADV_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief legacy_adv API
 * @defgroup legacy_adv_apis Bluetooth legacy advertising APIs
 * @{
 */

/**
 * @brief Start advertising on legacy Bluetooth channels
 *
 * Advertise the devices name on legacy Bluetooth channels.
 * This can help some legacy devices (and iOS) to connect.
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int bluetooth_legacy_advertising_run(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_LEGACY_ADV_H_ */
