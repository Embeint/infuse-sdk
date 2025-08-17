/**
 * @file
 * @brief ePacket specific data logging interface
 * @copyright 2025 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EPACKET_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EPACKET_H_

#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set ePacket flags for this backend
 *
 * @param dev ePacket data logger to configure
 * @param flags Bitmask of @ref epacket_flags or interface specific flags
 */
void logger_epacket_flags_set(const struct device *dev, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EPACKET_H_ */
