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

/** @brief Test device ID with fixed keys that cloud knows about */
#define INFUSE_TEST_DEVICE_ID 0xFFFFFFFFFFFFFFFDULL

/** @brief Address prefix for locally managed addesses */
#define INFUSE_LOCALLY_MANAGED_PREFIX 0xFFFF000000000000ULL

/** @brief Top two bits of a static random Bluetooth address must always be set */
#define BLUETOOTH_STATIC_RANDOM_PREFIX 0xC00000000000ULL

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
uint64_t infuse_device_id(void);

/**
 * @brief Convert a Bluetooth address to a locally managed Infuse-Iot device ID
 *
 * @param bt_addr 48 bit Bluetooth address to convert
 *
 * @return uint64_t Locally managed device ID
 */
static inline uint64_t local_infuse_device_id_from_bt(uint64_t bt_addr)
{
	return 0xFFFF000000000000ULL | bt_addr;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_IDENTIFIERS_H_ */
