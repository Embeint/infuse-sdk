/**
 * @file
 * @brief Infuse IoT device identifiers
 * @copyright 2024 Embeint Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse IoT identifier API
 * @defgroup infuse_identifier_apis Infuse IoT identifier APIs
 * @{
 */

/**
 * @brief Get local device ID
 *
 * @return uint64_t local device ID
 */
uint64_t infuse_device_id(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_ */
