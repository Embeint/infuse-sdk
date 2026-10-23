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
 * @ingroup infuse_dfu
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
 * If the flash device does not require an explicit erase, the image area is not erased.
 * Stale MCUboot metadata (data before the image start offset and the trailer) is still
 * cleared if @a mcuboot_trailer is set.
 *
 * @param fa Flash area to erase (must be already opened)
 * @param image_len Length of image, including any offset from the start of the area.
 *                  Zero erases nothing beyond the MCUboot metadata.
 * @param progress_cb Optional progress callback
 * @param mcuboot_trailer Clear stale MCUboot metadata: the MCUboot trailer, and on flash that
 *                        does not require an explicit erase, any data before the image start
 *                        offset (see @a boot_get_image_start_offset)
 *
 * @retval 0 On success
 * @retval -errno Error code from @a flash_get_page_info_by_offs or @a flash_area_flatten
 *                on failure
 */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len,
			   infuse_progress_cb_t progress_cb, bool mcuboot_trailer);

/**
 * @brief Clear stale MCUboot metadata around an image already present in a flash area
 *
 * Clears any data before the image start offset (see @a boot_get_image_start_offset) and the
 * MCUboot trailer, without modifying the image itself. Used when the image data already matches
 * but an upgrade still needs to be requested.
 *
 * @param fa Flash area containing the image (must be already opened)
 * @param image_end Offset of the end of the image within the flash area
 *
 * @retval 0 On success
 * @retval -EINVAL Image overlaps the MCUboot trailer, metadata cannot be cleared in place
 * @retval -ENOTSUP MCUboot trailer location unknown
 * @retval -errno Error code from @a flash_get_page_info_by_offs or @a flash_area_flatten
 *                on failure
 */
int infuse_dfu_image_metadata_clear(const struct flash_area *fa, size_t image_end);

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
