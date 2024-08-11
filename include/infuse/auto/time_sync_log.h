/**
 * @file
 * @brief Automatically log time sync events
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup time_sync_log_apis Auto: Time Sync Log
 * @{
 */

/**
 * @brief Automatically log time sync events
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 */
void auto_time_sync_log_configure(uint8_t tdf_logger_mask);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_TIME_SYNC_LOG_H_ */
