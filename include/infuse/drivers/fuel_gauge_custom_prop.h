/**
 * @file
 * @brief Custom fuel gauge properties
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Aeyohan Furtado <aeyohan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_FUEL_GAUGE_CUSTOM_PROP_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_FUEL_GAUGE_CUSTOM_PROP_H_

#include <zephyr/drivers/fuel_gauge.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief fuel_gauge_custom_prop API
 * @defgroup fuel_gauge_custom_prop_apis fuel_gauge_custom_prop APIs
 * @{
 */

enum fuel_gauge_custom_prop_type {
	FUEL_GAUGE_HIBERNATION_EN = FUEL_GAUGE_CUSTOM_BEGIN,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_FUEL_GAUGE_CUSTOM_PROP_H_ */
