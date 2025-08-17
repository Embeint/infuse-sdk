/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_GNSS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_GNSS_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup validation_gnss GNSS validation API
 * @{
 */

enum {
	/** Validate that GNSS powers up */
	VALIDATION_GNSS_POWER_UP = 0,
#ifdef CONFIG_GNSS_UBX_M8
	/** Burn the DC-DC converter fuse (this is a irreversible operation) */
	VALIDATION_GNSS_UBX_M8_DC_DC_BURN = BIT(7),
#endif /* CONFIG_GNSS_UBX_M8 */
};

/**
 * @brief Validate the behaviour of GNSS
 *
 * @param dev GNSS device
 * @param flags Validation tests to run
 *
 * @retval 0 On success
 * @retval -errno On failure
 */
int infuse_validation_gnss(const struct device *dev, uint8_t flags);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_VALIDATION_GNSS_H_ */
