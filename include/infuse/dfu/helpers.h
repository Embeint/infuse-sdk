/**
 * @file
 * @brief Infuse-IoT DFU helpers
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: LicenseRef-Embeint
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_

#include <stdlib.h>

#include <zephyr/storage/flash_map.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dfu_helpers_apis Infuse DFU helper APIs
 * @{
 */

/**
 * @brief Erase a flash area to be ready for a new image
 *
 * @param fa Flash area to erase (must be already opened)
 *
 * @retval 0 On success
 * @retval -errno Error code from @ref flash_area_erase on failure
 */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_ */
