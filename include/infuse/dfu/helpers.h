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
#include <stdbool.h>

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
 * @param image_len Length of image
 * @param mcuboot_trailer Erase space for MCUBoot trailer
 *
 * @retval 0 On success
 * @retval -errno Error code from @ref flash_area_erase on failure
 */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len, bool mcuboot_trailer);

/**
 * @brief Prepare the nRF91 modem for a delta image upgrade
 *
 * @retval 0 On success
 * @retval -errno Error code from nrf_modem_delta_dfu_offset, nrf_modem_delta_dfu_erase or
 * nrf_modem_delta_dfu_write_init
 */
int infuse_dfu_nrf91_modem_delta_prepare(void);

/**
 * @brief Finalise a nRF91 modem delta image upgrade
 *
 * @retval 0 On success
 * @retval -errno Error code from nrf_modem_delta_dfu_write_done or nrf_modem_delta_dfu_update
 */
int infuse_dfu_nrf91_modem_delta_finish(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_ */
