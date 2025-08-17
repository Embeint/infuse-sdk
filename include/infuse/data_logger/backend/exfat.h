/**
 * @file
 * @brief exFAT specific data logging interface
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
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
 * @param dev exFAT logging device to claim filesystem for
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

/**
 * @brief Stop logging to the current file, create the next file
 *
 * This function is intended for use when logging at extremely high frequencies,
 * when the file creation overhead is problematic.
 *
 * @warning This function does not operate from the same context as normal logging,
 *          so care must be taken that logging is not occurring at the same time to
 *          avoid corruption of the current block counter.
 *
 * @param dev exFAT logging device to move to the next file
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int logger_exfat_file_next(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DATA_LOGGER_BACKEND_EXFAT_H_ */
