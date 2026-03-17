/**
 * @file
 * @brief Automatic logging of KV Store data changes
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_KV_UPDATE_LOG_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_KV_UPDATE_LOG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief kv_update_log API
 * @defgroup kv_update_log_apis Logging of Key Value Store data changes
 * @{
 */

/**
 * @brief Automatically log Key Value Store data changes
 *
 * @note All calls to `kv_store_write` and `kv_store_delete` after this
 *       function call will result in logging. It is recommended to place
 *       this call after any functions that save default values to the KV
 *       store (e.g. @ref task_runner_init).
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 */
void auto_kv_update_log_configure(uint8_t tdf_logger_mask);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_KV_UPDATE_LOG_H_ */
