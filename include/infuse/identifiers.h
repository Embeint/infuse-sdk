/**
 * @file
 * @brief Infuse-IoT device identifiers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse-IoT identifier API
 * @defgroup infuse_identifier_apis Infuse-IoT identifier APIs
 * @{
 */

/** @cond INTERNAL_HIDDEN */
/**
 * @brief Expected vendor implementation of ID
 *
 * @return uint64_t local device ID
 */
uint64_t vendor_infuse_device_id(void);
/** @endcond */

/**
 * @brief Get local device ID
 *
 * @return uint64_t local device ID
 */
static inline uint64_t infuse_device_id(void)
{
#ifdef CONFIG_INFUSE_TEST_ID
	return 0xFFFFFFFFFFFFFFFDULL;
#else
	return vendor_infuse_device_id();
#endif /* CONFIG_INFUSE_TEST_ID */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_ */
