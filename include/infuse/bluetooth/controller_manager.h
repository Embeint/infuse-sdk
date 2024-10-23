/**
 * @file
 * @brief Manage Bluetooth controller not part of main application image
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_
#define INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief controller_manager API
 * @defgroup controller_manager_apis Bluetooth controller management APIs
 * @{
 */

/**
 * @brief Initialise management of Bluetooth controller
 *
 * @retval 0 on success
 * @retval -errno on error
 */
int bt_controller_manager_init(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_BLUETOOTH_CONTROLLER_MANAGE_H_ */
