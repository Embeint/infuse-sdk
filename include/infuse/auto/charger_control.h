/**
 * @file
 * @brief Automatically control battery charging based on temperature
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGER_CONTROL_H_
#define INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGER_CONTROL_H_

#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup charger_control_apis Auto: Charger control
 * @{
 */

/**
 * @brief Control logging of charge control events
 *
 * @param tdf_logger_mask TDF data logger mask to log events to
 */
void auto_charger_control_log_configure(uint8_t tdf_logger_mask);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_AUTO_CHARGER_CONTROL_H_ */
