/**
 * @file
 * @brief Common progress callback
 * @copyright 2025 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_UTIL_PROGRESS_CB_H_
#define INFUSE_SDK_INCLUDE_INFUSE_UTIL_PROGRESS_CB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Common progress callback type definition
 * @{
 */

/**
 * @brief Common progress callback type
 *
 * @param progress Currently completed progress towards total
 * @param total Total amount of work to complete
 */
typedef void (*infuse_progress_cb_t)(uint32_t progress, uint32_t total);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_UTIL_PROGRESS_CB_H_ */
