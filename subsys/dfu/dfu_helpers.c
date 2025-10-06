/**
 * @file
 * @copyright 2024 Embeint Holdings Pty Ltd
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>

#ifdef CONFIG_NRF_MODEM_LIB
#include "nrf_modem_delta_dfu.h"
#endif /* CONFIG_NRF_MODEM_LIB */

/* Implementation taken from zephyr_img_mgmt.c */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len,
			   void (*progress_callback)(uint32_t offset), bool mcuboot_trailer)
{
	const struct device *dev = flash_area_get_device(fa);
	struct flash_pages_info page;
	size_t remaining;
	size_t max_chunk;
	size_t chunk_size;
	size_t off;
	off_t page_offset;
	size_t erase_size;
	int rc;

	if (dev == NULL) {
		return -ENODEV;
	}

	/* Validate length fits within flash area */
	if (image_len > fa->fa_size) {
		return -EINVAL;
	}
	page_offset = fa->fa_off + image_len - 1;

	/* align requested erase size to the erase-block-size */
	rc = flash_get_page_info_by_offs(dev, page_offset, &page);
	if (rc < 0) {
		return rc;
	}
	erase_size = page.start_offset + page.size - fa->fa_off;

	/* Perform the erase */
	max_chunk = progress_callback ? CONFIG_INFUSE_DFU_HELPERS_ERASE_CHUNK_SIZE : erase_size;
	remaining = erase_size;
	off = 0;
	while (remaining) {
		/* Erase next chunk */
		chunk_size = MIN(max_chunk, remaining);
		rc = flash_area_flatten(fa, off, chunk_size);
		if (rc < 0) {
			return rc;
		}
		off += chunk_size;
		remaining -= chunk_size;
		/* Run provided callback */
		if (progress_callback) {
			progress_callback(off);
		}
	}

	if (!mcuboot_trailer) {
		return 0;
	}

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	off = boot_get_trailer_status_offset(fa->fa_size);
	if (off < erase_size) {
		/* Trailer area has already been erased */
		return 0;
	}
	(void)flash_get_page_info_by_offs(dev, fa->fa_off + off, &page);
	off = page.start_offset - fa->fa_off;
	erase_size = fa->fa_size - off;

	return flash_area_flatten(fa, off, erase_size);
#elif defined(CONFIG_ZTEST)
	/* Pretend there is a trailer 64 bytes from the end */
	off = fa->fa_size - 64;

	(void)flash_get_page_info_by_offs(dev, fa->fa_off + off, &page);
	off = page.start_offset - fa->fa_off;
	erase_size = fa->fa_size - off;

	return flash_area_flatten(fa, off, erase_size);
#else
	return -ENOTSUP;
#endif
}

#ifdef CONFIG_NRF_MODEM_LIB
int infuse_dfu_nrf91_modem_delta_prepare(void)
{
	size_t offset;
	int rc;

	/* Determine if area needs to be erased first */
	rc = nrf_modem_delta_dfu_offset(&offset);
	if (rc != 0) {
		return rc;
	}
	/* We don't support resuming an interrupted download.
	 * Any value other than 0 needs to be erased
	 */
	if (offset != 0) {
		/* Erase area if required */
		rc = nrf_modem_delta_dfu_erase();
		if (rc != 0) {
			return rc;
		}
	}
	/* Wait for DFU system to be ready.
	 * If for some reason the erase never finishes, the watchdog will catch us.
	 */
	while (offset != 0) {
		rc = nrf_modem_delta_dfu_offset(&offset);
		if ((rc != 0) && (rc != NRF_MODEM_DELTA_DFU_ERASE_PENDING)) {
			return rc;
		}
		k_sleep(K_MSEC(500));
	}
	/* Ready modem to receive the firmware update */
	rc = nrf_modem_delta_dfu_write_init();
	if ((rc != 0) && (rc != -EALREADY)) {
		return rc;
	}
	return 0;
}

int infuse_dfu_nrf91_modem_delta_finish(void)
{
	int rc;

	/* Free resources */
	rc = nrf_modem_delta_dfu_write_done();
	if (rc == 0) {
		/* Schedule the update for next reboot */
		rc = nrf_modem_delta_dfu_update();
	}
	return rc;
}
#endif /* CONFIG_NRF_MODEM_LIB */
