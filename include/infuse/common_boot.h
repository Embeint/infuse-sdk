/**
 * @file
 * @brief Interactions with the common boot logic
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_

#include <infuse/reboot.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief infuse_common_boot API
 * @defgroup infuse_common_boot_apis Common boot APIs
 * @{
 */

/**
 * @brief Query the reason for the latest reboot
 *
 * @note Unlike @ref infuse_reboot_state_query, this function can be called
 *       as many times as desired.
 *
 * @note Even when returning -ENOENT, the @a reason and @a hardware_reason
 *       struct field are still valid.
 *
 * @param state Output location for reboot information
 *
 * @retval 0 On successful state query
 * @retval -ENOENT No reboot information exists
 */
int infuse_common_boot_last_reboot(struct infuse_reboot_state *state);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_ */
