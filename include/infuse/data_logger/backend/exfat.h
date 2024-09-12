/**
 * @file
 * @brief exFAT specific data logging interface
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EXFAT_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EXFAT_H_

#include <zephyr/device.h>
#include <ff.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Claim exFAT filesystem
 *
 * @param dev exFAT logging device to claim filesysem for
 * @param buf Block buffer that can be used for reads (Can be NULL if not needed)
 * @param buf_size Size of the block buffer (Must be non-NULL if @a buf provided)
 * @param timeout Duration to wait for filesystem object
 *
 * @retval disk name on success
 * @retval NULL on timeout
 */
const char *logger_exfat_filesystem_claim(const struct device *dev, uint8_t **buf, size_t *buf_size,
					  k_timeout_t timeout);

/**
 * @brief Release filesystem object claimed with @ref logger_exfat_filesystem_claim
 *
 * @param dev exFAT logging device to release
 */
void logger_exfat_filesystem_release(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EXFAT_H_ */
