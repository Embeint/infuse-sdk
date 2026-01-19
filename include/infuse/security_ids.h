/**
 * @file
 * @brief Infuse Platform Security Identifiers
 * @copyright 2026 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 *
 * @details
 */

#include <stdint.h>

#ifndef INFUSE_SDK_INCLUDE_INFUSE_SECURITY_IDS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_SECURITY_IDS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infuse security API
 * @defgroup infuse_security_apis Infuse security APIs
 * @{
 */
/**
 * @brief Get the current device key identifier
 *
 * The device key identifier is constructed as a CRC32 hash computed over the
 * cloud and device public keys, truncated to 24 bits.
 *
 * @return uint32_t 24bit device key identifier
 */
uint32_t infuse_security_device_key_identifier(void);

/**
 * @brief Get the current secondary device key identifier
 *
 * The device key identifier is constructed as a CRC32 hash computed over the
 * remote and device public keys, truncated to 24 bits.
 *
 * @return uint32_t 24bit secondary device key identifier
 */
uint32_t infuse_security_secondary_device_key_identifier(void);

/**
 * @brief Get the current network key identifier
 *
 * @return uint32_t 24 bit network key identifier
 */
uint32_t infuse_security_network_key_identifier(void);

/**
 * @brief Get the secondary network key identifier
 *
 * Depends on CONFIG_INFUSE_SECURITY_SECONDARY_NETWORK_ENABLE.
 *
 * @return uint32_t 24 bit network key identifier
 */
uint32_t infuse_security_secondary_network_key_identifier(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_SECURITY_IDS_H_ */
