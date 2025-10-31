/**
 * @file
 * @brief Infuse-IoT DFU helpers
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: FSL-1.1-ALv2
 */

#ifndef INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_
#define INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_

#include <stdlib.h>
#include <stdbool.h>

#include <zephyr/storage/flash_map.h>

#include <infuse/util/progress_cb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dfu_helpers_apis Infuse DFU helper APIs
 * @{
 */

/**
 * @brief Prepare a flash area for write/erase
 *
 * Prepare to erase or write to a flash area.
 *
 * @note MUST call @ref infuse_dfu_write_erase_finish when complete
 *
 * @param fa Flash area (must be already opened)
 */
void infuse_dfu_write_erase_start(const struct flash_area *fa);

/**
 * @brief Finalise a flash area write/erase
 *
 * Cleanup the @ref infuse_dfu_write_erase_start call once complete.
 *
 * @param fa Flash area (must be already opened)
 */
void infuse_dfu_write_erase_finish(const struct flash_area *fa);

/**
 * @brief Erase a flash area to be ready for a new image
 *
 * @param fa Flash area to erase (must be already opened)
 * @param image_len Length of image
 * @param progress_cb Optional progress callback
 * @param mcuboot_trailer Erase space for MCUBoot trailer
 *
 * @retval 0 On success
 * @retval -errno Error code from @a flash_area_erase on failure
 */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len,
			   infuse_progress_cb_t progress_cb, bool mcuboot_trailer);

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

#ifdef CONFIG_ZTEST

/**
 * @brief Get the current balance count of the start/finish helpers
 *
 * @return int Number of start calls minus finish calls
 */
int infuse_dfu_write_erase_call_count(void);

#endif /* CONFIG_ZTEST */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* INFUSE_SDK_INCLUDE_INFUSE_DFU_HELPERS_H_ */
