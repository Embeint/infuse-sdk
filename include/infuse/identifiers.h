/**
 * @file
 * @brief EIS device identifiers
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
 * @brief EIS identifier API
 * @defgroup eis_identifier_apis EIS identifier APIs
 * @{
 */

/**
 * @brief Get local device ID
 *
 * @return uint32_t local device ID
 */
uint32_t eis_device_id(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_ */
