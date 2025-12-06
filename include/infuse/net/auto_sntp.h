/**
 * @file
 * @brief Automatic SNTP functionality
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_NET_AUTO_SNTP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_NET_AUTO_SNTP_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Automatic SNTP functionality
 * @defgroup auto_sntp_apis Automatic SNTP functionality
 * @{
 */

/**
 * @brief Notify auto SNTP library that it is allowed to send data
 *
 * Only valid for @kconfig{SNTP_AUTO_SYNC_POINTS}.
 */
void sntp_auto_sync_point(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_NET_AUTO_SNTP_H_ */
