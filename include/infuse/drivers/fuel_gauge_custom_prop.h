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
 * @brief Custom fuel gauge properties
 * @defgroup fuel_gauge_custom_prop_apis Custom fuel gauge properties
 * @ingroup infuse_drivers
 * @{
 */

/** @brief Infuse-IoT custom fuel gauge property types */
enum fuel_gauge_custom_prop_type {
	/** Enable (non-zero) or disable (zero) hibernation mode, value in `sbs_mode` */
	FUEL_GAUGE_HIBERNATION_EN = FUEL_GAUGE_CUSTOM_BEGIN,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DRIVERS_FUEL_GAUGE_CUSTOM_PROP_H_ */
