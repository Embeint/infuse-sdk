/**
 * @file
 * @copyright 2024 Embeint Inc
 * @author Jordan Yates <jordan@embeint.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/mcuboot.h>

#include <infuse/dfu/helpers.h>

/* Implementation taken from zephyr_img_mgmt.c */
int infuse_dfu_image_erase(const struct flash_area *fa, size_t image_len)
{
	const struct device *dev = flash_area_get_device(fa);
	off_t page_offset = fa->fa_off + image_len - 1;
	struct flash_pages_info page;
	size_t erase_size;
	size_t off;
	int rc;

	if (dev == NULL) {
		return -ENODEV;
	}

	/* align requested erase size to the erase-block-size */
	rc = flash_get_page_info_by_offs(dev, page_offset, &page);
	if (rc < 0) {
		return rc;
	}
	erase_size = page.start_offset + page.size - fa->fa_off;

	/* Perform the erase */
	rc = flash_area_flatten(fa, 0, erase_size);
	if (rc < 0) {
		return rc;
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
#error Unknown image management scheme
#endif
}
