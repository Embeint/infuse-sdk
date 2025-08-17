/**
 * @file
 * @brief Interactions with the common boot logic
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_

#ifdef CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY
#include <tfm_ioctl_api.h>
#endif

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

#ifdef CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY

/**
 * @brief Query the secure fault information for the latest reboot
 *
 * @param fault_info Output location for secure fault information
 *
 * @retval 0 On successful fault info query
 * @retval -ENOENT If the previous reboot was not a secure fault
 */
int infuse_common_boot_secure_fault_info(struct fault_exception_info_t *fault_info);

#endif /* CONFIG_TFM_PLATFORM_FAULT_INFO_QUERY */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_COMMON_BOOT_H_ */
